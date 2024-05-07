.PHONY: run clean debug

$(error "Outdated. Use Meson")

CXXFLAGS := -std=c++20 -g -DDEBUG -Wno-attributes
CXXFLAGS += $(shell pkg-config spdlog fmt wayland-client --cflags)
LDFLAGS := $(shell pkg-config spdlog fmt wayland-client --libs)

SOURCES := slideshow.cpp wayland-gen/xdg-shell.c image_pam.cpp utils.cpp

slideshow: $(SOURCES)
	g++ $(CXXFLAGS) $(LDFLAGS) $^ -o $@

run: slideshow
	exec ./slideshow

debug: slideshow
	exec gdb ./slideshow

clean:
	-rm -f ./slideshow
