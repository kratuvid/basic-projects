#include <iostream>
#include <cassert>
#include <cstring>
#include <sstream>
#include <limits>
#include <source_location>
#include <typeinfo>
#include <random>
#include <exception>
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <spdlog/spdlog.h>
#include <wayland-client.h>
#include "wayland-gen/xdg-shell.h"
#include "utils.hpp"

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

		memset(app->shm_mmap, 0, app->shm_size);
		
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

		std::string random_filename("/slideshow-XXXXXX");
		std::random_device rd;
		
		do
		{
			for (std::string::size_type i = random_filename.length() - 6; i < random_filename.length(); i++)
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
			if (shm_mmap != MAP_FAILED || shm_mmap != nullptr)
				munmap(shm_mmap, shm_size);
			if (fd && fd != -1)
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
	const float wait_next_sort = 100.f / 1e3;

	using chosen_type = int;
	
	std::array<chosen_type, 50> unsorted;
	const chosen_type unsorted_range[2] {1, 500};
	chosen_type unsorted_minmax[2];
	
 	const int margins[2][2] {{10, 10}, {50, 10}};
	const uint32_t bar_colour_normal = 0x000000ff;
	const uint32_t bar_colour_done = 0x0000ff00;
	const uint32_t bar_colour_touching = 0x00ff0000;
	const uint32_t bar_colour_border = 0x00fff000;
	
	void init_rest()
	{
		build_array();
	}

	void build_array()
	{
		std::random_device rd;
		std::default_random_engine rand_engine(rd());
		std::uniform_int_distribution<chosen_type> uniform_distributor(unsorted_range[0], unsorted_range[1]);
		
		unsorted_minmax[0] = std::numeric_limits<chosen_type>::max();
		unsorted_minmax[1] = std::numeric_limits<chosen_type>::min();
	
		for (auto& element : unsorted)
		{
			element = uniform_distributor(rand_engine);
			if (element < unsorted_minmax[0]) unsorted_minmax[0] = element;
			if (element > unsorted_minmax[1]) unsorted_minmax[1] = element;
		}
		
		std::ostringstream oss;
		oss << "Unsorted array: ";
		for (auto element : unsorted)
			oss << element << " ";
		spdlog::info("{}", oss.str());
		spdlog::info("Minmax: {} {}", unsorted_minmax[0], unsorted_minmax[1]);
	}

	void sort()
	{
		static unsigned i = 0;
		
		if (i >= unsorted.size())
		{
			i = 0;
			build_array();
		}

		for (unsigned j=0; j < unsorted.size(); j++)
		{
			if (i == j) break;
			if (unsorted[j] > unsorted[i])
				std::swap(unsorted[i], unsorted[j]);
		}
		i++;

		std::ostringstream oss;
		oss << "Pass " << i << ": ";
		for (auto element : unsorted)
			oss << element << " ";
		spdlog::info("{}", oss.str());
	}

	void draw(uint32_t timems)
	{
		static auto last = timems;
		auto now = timems;
		float delta_secs = float(now - last) / 1e3;
		last = now;

		bool is_next_sort = false;
		static float accum = 0;
		if (accum > wait_next_sort)
		{
			is_next_sort = true;
			accum = 0.f;
		}
		accum += delta_secs;

		memset(shm_mmap, 0, shm_size);

		if (is_next_sort)
		{
			sort();
		}

		// Draw the bars
		const int how_lr = margins[0][0] + margins[0][1];
		const int how_tb = margins[1][0] + margins[1][1];
		const int bar_width = (width - how_lr) / unsorted.size();
		const int bar_height_max = height - how_tb;
		
		for (unsigned i = 0; i < unsorted.size(); i++)
		{
			const int size_y = bar_height_max * (unsorted[i] / float(unsorted_minmax[1]));
			// draw_rectangle(margins[0][0] + bar_width * i, margins[1][1], bar_width, size_y, bar_colour_normal);
			draw_rectangle_bordered(margins[0][0] + bar_width * i, margins[1][1], bar_width, size_y, bar_colour_normal, 2, 3, bar_colour_border);
		}
			
		wl_surface_attach(w_surface, w_buffer, 0, 0);
		wl_surface_damage_buffer(w_surface, 0, 0, width, height);
	}

	void draw_rectangle(unsigned x, unsigned y, unsigned size_x, unsigned size_y, uint32_t colour)
	{
		auto shm_mmap32 = static_cast<uint32_t*>(shm_mmap);
		for (size_t i=y; i < y+size_y; i++)
		{
			for (size_t j=x; j < x+size_x; j++)
			{
				shm_mmap32[coord_to_location(j, i)] = colour;
			}
		}
	}

	void draw_rectangle_bordered(unsigned x, unsigned y, unsigned size_x, unsigned size_y, uint32_t colour, unsigned border_size_x, unsigned border_size_y, uint32_t colour_border)
	{
		diffassert_msg(border_size_x*2 < size_x, fmt::format("border_size_x:{} size_x:{}", border_size_x, size_x));
		draw_rectangle(x, y, size_x, size_y, colour_border);
		if (border_size_y*2 < size_y)
			draw_rectangle(x+border_size_x, y+border_size_y, size_x-border_size_x*2, size_y-border_size_y*2, colour);
	}

	size_t coord_to_location(unsigned x, unsigned y)
	{
		diffassert_msg(x < unsigned(width), fmt::format("x:{} width:{}", x, width));
		diffassert_msg(y < unsigned(height), fmt::format("y:{} height:{}", y, height));
		return (y * width) + x;
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
		if (shm_mmap != MAP_FAILED || shm_mmap != nullptr)
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
