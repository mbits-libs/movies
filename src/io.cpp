// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <io/file.hpp>

namespace io {
	file::ptr file::open(fs::path const& filename, char const* mode) {
#ifdef WIN32
		auto const length = std::strlen(mode) + 1;
		auto wmode = std::make_unique<wchar_t[]>(length);
		for (size_t ndx = 0; ndx < length; ++ndx)
			wmode[ndx] = mode[ndx];

		FILE* fptr{};
		if (_wfopen_s(&fptr, filename.native().c_str(), wmode.get()))
			fptr = nullptr;
		return ptr{fptr};
#else
		return ptr{std::fopen(filename.native().c_str(), mode)};
#endif
	}

	std::vector<char8_t> contents(fs::path const& path) {
		auto json = file::open(path, "rb");
		if (!json) return {};
		return contents(json);
	}

	std::vector<char8_t> contents(io::file::ptr const& json) {
		std::vector<char8_t> result;

		char8_t buffer[8192];  // NOLINT(readability-magic-numbers)
		size_t read{};
		while ((read = std::fread(buffer, 1, sizeof(buffer), json.get())) !=
		       0) {
			result.insert(result.end(), buffer, buffer + read);
		}

		return result;
	}
}  // namespace io
