#include <iostream>
#include <cassert>
#include <cstring>
#include <source_location>
#include <random>
#include <exception>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <spdlog/spdlog.h>
#include <wayland-client.h>
#include "wayland-gen/xdg-shell.h"
#include "utils.hpp"
#include "image_pam.hpp"

class App
{
private: /* variables */
	/* Wayland */
	// Core globals
	wl_display* w_display = nullptr;
	wl_registry* w_registry = nullptr;
	// Necessary globals
	wl_shm* w_shm = nullptr;
	wl_compositor* w_compositor = nullptr;
	xdg_wm_base* w_xdg_wm_base = nullptr;
	// Supplimentary
	wl_surface* w_surface = nullptr;
	xdg_surface* w_xdg_surface = nullptr;
	xdg_toplevel* w_xdg_toplevel = nullptr;
	wl_buffer* w_buffer = nullptr;

	// Wayland listener structs
	const struct wl_registry_listener w_registry_listener = {
		.global = w_registry_listener_global,
		.global_remove = w_registry_listener_global_remove
	};
	const struct xdg_wm_base_listener w_xdg_wm_base_listener = {
		.ping = w_xdg_wm_base_listener_ping
	};
	const struct xdg_surface_listener w_xdg_surface_listener = {
		.configure = w_xdg_surface_listener_configure
	};
	const struct xdg_toplevel_listener w_xdg_toplevel_listener = {
		.configure = w_xdg_toplevel_listener_configure,
		.close = w_xdg_toplevel_listener_close
	};
	const struct wl_callback_listener w_frame_callback_listener = {
		.done = w_frame_callback_listener_done
	};

	bool quit = false;
	int width = 0, height = 0;
	size_t shm_size = 0;
	void* shm_mmap = nullptr;
	
public: /* static listener functions */
	static void w_registry_listener_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
	{
		auto app = static_cast<App*> (data);
		
		if (strcmp(interface, wl_shm_interface.name) == 0)
			app->w_shm = static_cast<wl_shm*> (wl_registry_bind(registry, name, &wl_shm_interface, 1));
		else if (strcmp(interface, wl_compositor_interface.name) == 0)
			app->w_compositor = static_cast<wl_compositor*> (wl_registry_bind(registry, name, &wl_compositor_interface, 5));
		else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
			app->w_xdg_wm_base = static_cast<xdg_wm_base*> (wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
	}
	static void w_registry_listener_global_remove(void* data, wl_registry* registry, uint32_t name)
	{
	}

	static void w_xdg_wm_base_listener_ping(void* data, xdg_wm_base* base, uint32_t serial)
	{
		xdg_wm_base_pong(base, serial);
	}

	static void w_xdg_surface_listener_configure(void* data, xdg_surface* surface, uint32_t serial)
	{
		xdg_surface_ack_configure(surface, serial);
	}

	static void w_xdg_toplevel_listener_configure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states)
	{
		auto app = static_cast<App*>(data);

		if (width == 0 || height == 0)
			return;
		if (app->width == width && app->height == height)
			return;

		app->width = width;
		app->height = height;
		
		if (app->shm_mmap != MAP_FAILED && app->shm_mmap != nullptr)
		{
			munmap(app->shm_mmap, app->shm_size);
			app->shm_mmap = nullptr;
		}
		if (app->w_buffer)
			wl_buffer_destroy(app->w_buffer);
		
		app->w_buffer = app->create_buffer();
		diffassert(app->w_buffer);
		wl_surface_attach(app->w_surface, app->w_buffer, 0, 0);

		memset(app->shm_mmap, height, app->shm_size);
		
		wl_surface_commit(app->w_surface);
	}
	static void w_xdg_toplevel_listener_close(void* data, xdg_toplevel* toplevel)
	{
		static_cast<App*>(data)->quit = true;
	}

	static void w_frame_callback_listener_done(void* data, wl_callback* callback, uint32_t timems)
	{
		auto app = static_cast<App*>(data);

		app->draw(timems);

		wl_callback* next_frame_callback = wl_surface_frame(app->w_surface);
		diffassert(next_frame_callback);
		wl_callback_add_listener(next_frame_callback, &app->w_frame_callback_listener, data);
		
		wl_surface_commit(app->w_surface);
	}

private: /* private member functions */
	int create_anon_shm()
	{
		int retries = 100;

		std::string random_filename("/" __FILE__ "-XXXXXX");
		std::random_device rd;
		
		do
		{
			for (int i = random_filename.length() - 6; i < random_filename.length(); i++)
				random_filename[i] = static_cast<char>( (rd() % 26) + 'A' );

			int fd = shm_open(random_filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
			if (fd == -1)
			{
				if (errno == EEXIST)
					continue;
				else
					return -1;
			}
			else
			{
				shm_unlink(random_filename.c_str());
				return fd;
			}
		} while (--retries > 0);

		return -1;
	}
	
	wl_buffer* create_buffer()
	{
		diffassert(width != 0);
		diffassert(height != 0);

		int fd = 0;
		wl_shm_pool* shm_pool = nullptr;
		wl_buffer* new_buffer = nullptr;
		
		const int stride = width * 4;
		shm_size = stride * height;

		try
		{
			fd = create_anon_shm();
			diffassert(fd != -1);

			int fret = ftruncate(fd, shm_size);
			diffassert(fret != -1);

			shm_mmap = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			diffassert(shm_mmap != MAP_FAILED);

			shm_pool = wl_shm_create_pool(w_shm, fd, shm_size);
			diffassert(shm_pool);

			new_buffer = wl_shm_pool_create_buffer(shm_pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
		}
		catch (const bool& e)
		{
			if (shm_pool)
				wl_shm_pool_destroy(shm_pool);
			if (shm_mmap != MAP_FAILED && shm_mmap != nullptr)
				munmap(shm_mmap, shm_size);
			if (fd)
				close(fd);
			throw;
		}

		wl_shm_pool_destroy(shm_pool);
		close(fd);

		return new_buffer;
	}

	void init_wayland()
	{
		// Initialize core wayland
		w_display = wl_display_connect(nullptr);
		diffassert(w_display);

		w_registry = wl_display_get_registry(w_display);
		diffassert(w_registry);

		wl_registry_add_listener(w_registry, &w_registry_listener, this);
		diffassert(wl_display_roundtrip(w_display) != -1);
		
		diffassert(w_xdg_wm_base);
		diffassert(w_compositor);
		diffassert(w_shm);

		xdg_wm_base_add_listener(w_xdg_wm_base, &w_xdg_wm_base_listener, this);

		// Initialize rest of wayland
		w_surface = wl_compositor_create_surface(w_compositor);
		diffassert(w_surface);
		w_xdg_surface = xdg_wm_base_get_xdg_surface(w_xdg_wm_base, w_surface);
		diffassert(w_xdg_surface);
		w_xdg_toplevel = xdg_surface_get_toplevel(w_xdg_surface);
		diffassert(w_xdg_toplevel);

		xdg_surface_add_listener(w_xdg_surface, &w_xdg_surface_listener, this);
		xdg_toplevel_add_listener(w_xdg_toplevel, &w_xdg_toplevel_listener, this);
		wl_surface_commit(w_surface);
		diffassert(wl_display_roundtrip(w_display) != -1);
		
		w_buffer = create_buffer();
		diffassert(w_buffer);
		wl_surface_attach(w_surface, w_buffer, 0, 0);
		wl_surface_commit(w_surface);
		diffassert(wl_display_roundtrip(w_display) != -1);

		wl_callback* first_surface_frame_callback = wl_surface_frame(w_surface);
		diffassert(first_surface_frame_callback);
		wl_callback_add_listener(first_surface_frame_callback, &w_frame_callback_listener, this);
		wl_surface_commit(w_surface);
	}

private:
	image_pam images[10];

	void init_rest()
	{
		for (int i=0; i < 10; i++)
			images[i].parse(fmt::format("slideshow_images/{}.pam", std::to_string(i+1)));
	}
	
public:
	App(int width, int height) :width(width), height(height) {}

	void init()
	{
		init_wayland();
		init_rest();
	}
	
	~App()
	{
		if (shm_mmap != MAP_FAILED && shm_mmap != nullptr)
			munmap(shm_mmap, shm_size);
		if (w_buffer)
			wl_buffer_destroy(w_buffer);
		
		if (w_xdg_toplevel)
			xdg_toplevel_destroy(w_xdg_toplevel);
		if (w_xdg_surface)
			xdg_surface_destroy(w_xdg_surface);
		if (w_surface)
			wl_surface_destroy(w_surface);
		
		if (w_xdg_wm_base)
			xdg_wm_base_destroy(w_xdg_wm_base);
		if (w_compositor)
			wl_compositor_destroy(w_compositor);
		if (w_shm)
			wl_shm_destroy(w_shm);
		
		if (w_registry)
			wl_registry_destroy(w_registry);
		if (w_display)
			wl_display_disconnect(w_display);
	}

	void draw(uint32_t timems)
	{
		static auto last = timems;
		auto now = timems;
		auto deltams = now - last;
		last = now;
		
		static int index = 0;
		static auto accum = 0;
		accum += deltams;
		if (accum > 750)
		{
			accum = 0;
			index++;
		}
		index = index % 10;
		
		memset(shm_mmap, 0, shm_size);

		const size_t pixel_stride = width * 4;
		const size_t image_stride = images[index].hdr.width * 4;
		auto pixels = static_cast<uint8_t*>(shm_mmap);
		
		for (size_t i=0; i < std::min((uint16_t)height, images[index].hdr.height); i++)
		{
			const size_t pixel_location = i * pixel_stride;
			const size_t image_location = i * image_stride;
			memcpy(pixels + pixel_location, images[index].data.get() + image_location, std::min(pixel_stride, image_stride));
		}

		wl_surface_attach(w_surface, w_buffer, 0, 0);
		wl_surface_damage_buffer(w_surface, 0, 0, std::min((uint16_t)width, images[index].hdr.width), std::min((uint16_t)height, images[index].hdr.height));
	}

	void run()
	{
		while (wl_display_dispatch(w_display) != -1 && !quit)
		{
		}
	}
};

int main()
{
	spdlog::set_level(spdlog::level::debug);
	
	App app(800, 600);
	try
	{
		app.init();
		app.run();
	}
	catch (const bool& e)
	{
		std::cerr << "`errno` says: " << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}
}
