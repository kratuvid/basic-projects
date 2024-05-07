#pragma once

#include <iostream>
#include <string_view>
#include <cstdint>
#include <fstream>
#include <memory>
#include <array>
#include <regex>
#include <string>
#include <exception>
#include <initializer_list>
#include <utility>
#include <cstring>
#include <spdlog/spdlog.h>

#include "utils.hpp"

/* Current impl only supports rgb and rgb_alpha types */
class image_pam
{
public:
	struct header
	{
		uint16_t width, height;
		uint16_t depth;
		uint16_t maxval;
		uint16_t tupltype; // RGB:1, RGB_ALPHA:2
		// RGB is stored as XRGB
	} hdr {};
	
	std::unique_ptr<uint8_t[]> data;
	size_t size;
	
public:
	void parse(const std::string_view& filename);
};
