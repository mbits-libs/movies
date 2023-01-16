// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace io {
	struct file {
		struct closer {
			void operator()(FILE* ptr) { std::fclose(ptr); }
		};

		using ptr = std::unique_ptr<FILE, closer>;

		static ptr open(fs::path const&, char const* mode);
	};

	std::vector<char8_t> contents(fs::path const&);
	std::vector<char8_t> contents(io::file::ptr const&);
}  // namespace io
