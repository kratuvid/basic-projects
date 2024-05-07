#include "image_pam.hpp"

void image_pam::parse(const std::string_view& filename)
{
	data.reset();
	size = 0;
	
	std::ifstream ifs(filename.data(), std::ios::binary | std::ios::in);
	diffassert_msg(ifs.is_open(), fmt::format("Failed to open PAM image {} for parsing", filename));

	memset(&hdr, 0, sizeof(hdr));
	const std::initializer_list<std::pair<std::regex, uint16_t*>> reg_values {
		{std::regex(R"(WIDTH\s(\d+))"), &hdr.width},
		{std::regex(R"(HEIGHT\s(\d+))"), &hdr.height},
		{std::regex(R"(DEPTH\s(\d+))"), &hdr.depth},
		{std::regex(R"(MAXVAL\s(\d+))"), &hdr.maxval},
		{std::regex(R"(TUPLTYPE\s(.+))"), &hdr.tupltype},
		{std::regex(R"((ENDHDR))"), nullptr}
	};
	std::cmatch match;

	bool encountered_endhdr = false;

	std::array<char, 50> some;
	ifs.getline(some.data(), some.size());
	diffassert_msg(some[0] == 'P' && some[1] == '7', fmt::format("{} is not a PAM image file", filename));

	while (true)
	{
		memset(some.data(), 0, sizeof(char) * some.size());
		
		if(ifs.getline(some.data(), some.size()))
		{
			bool matched_any = false;
			for (const auto& p : reg_values)
			{
				if (std::regex_match(some.data(), match, p.first))
				{
					matched_any = true;
					
					if (p.second == nullptr)
					{
						encountered_endhdr = true;
						goto process_data;
					}
					else if (match.size() == 2)
					{
						const auto arg = match[1].str();

						if (p.second == &hdr.tupltype)
						{
							if (arg == "RGB")
								*p.second = 1;
							else if (arg == "RGB_ALPHA")
								*p.second = 2;
							else
								diffassert_msg(false, fmt::format("Unsupported PAM header tupltype ({}) in file {}", some.data(), filename));
						}
						else
						{
							try {
								*p.second = std::stoi(arg);
							} catch (std::exception& e)
							{
								diffassert_msg(false, fmt::format("Failed to convert PAM header element ({}) argument into an integer in file {}", some.data(), filename));
							}
						}
					}
					
					break;
				}
			}

			diffassert_msg(matched_any, fmt::format("Unknown PAM header element ({}) in file {}", some.data(), filename));
		}
		else
		{
			diffassert_msg(false, fmt::format("End of file reached while parsing PAM header in file {}", filename));
		}
	}

	diffassert(encountered_endhdr);

  process_data:
	diffassert_msg(hdr.width != 0, fmt::format("In PAM image {}", filename));
	diffassert_msg(hdr.height != 0, fmt::format("In PAM image {}", filename));
	diffassert_msg(hdr.depth != 0, fmt::format("In PAM image {}", filename));
	diffassert_msg(hdr.maxval != 0, fmt::format("In PAM image {}", filename));
	diffassert_msg(hdr.tupltype != 0, fmt::format("In PAM image {}", filename));

	diffassert_msg(hdr.maxval == 255, fmt::format("In {}, PAM header maxvals other than 255 are unsupported", filename));
	
	spdlog::debug("Parsed PAM image header in {}: Width: {}, Height: {}, Depth: {}, Maxval: {}, Tupltype: {}", filename, hdr.width, hdr.height, hdr.depth, hdr.maxval, hdr.tupltype);

	const size_t bytes_per_pixel = hdr.tupltype == 1 ? 3 : 4;
	const size_t expected_size = bytes_per_pixel * hdr.width * hdr.height;

	auto cur = ifs.tellg();
	ifs.seekg(0, std::ios::end);
	auto last = ifs.tellg();
	ifs.seekg(cur);

	diffassert_msg(expected_size == (last-cur), fmt::format("Expected PAM file {} of size {} bytes, has {} bytes", filename, expected_size, last-cur));

	const size_t actual_stride = hdr.width * 4;
	size = actual_stride * hdr.height;
	data = std::make_unique<uint8_t[]>(size);

	const size_t stride = hdr.width * bytes_per_pixel;
	auto stride_data = std::make_unique<uint8_t[]>(stride);
	
	for (size_t i=0, l=0; i < expected_size; i += stride, l += actual_stride)
	{
		ifs.read(reinterpret_cast<char*>(stride_data.get()), stride);
		if (hdr.tupltype == 1)
		{
			for (size_t j=0, m=0; j < stride; j += 3, m += 4)
			{
				data[l + m + 0] = stride_data[j+2]; // blue
				data[l + m + 1] = stride_data[j+1]; // green
				data[l + m + 2] = stride_data[j+0]; // red
				data[l + m + 3] = 0xff;
			}
		}
		else
		{
			memcpy(data.get() + l, stride_data.get(), stride);
		}
	}
}
