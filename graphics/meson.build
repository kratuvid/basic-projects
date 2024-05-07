project('basic_projects/graphics', 'c', 'cpp', default_options: ['cpp_std=c++20'])
deps = [dependency('wayland-client'), dependency('spdlog'), dependency('fmt')]
executable('slideshow', 'slideshow.cpp', 'image_pam.cpp', 'utils.cpp', 'wayland-gen/xdg-shell.c', dependencies: deps)