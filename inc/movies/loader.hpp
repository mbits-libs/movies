// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include "fwd.hpp"
#include "movie_info.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace movies {
	struct file_ref {
		string id;
		fs::file_time_type mtime;
	};

	struct movie_data {
		movie_info info{};

		std::optional<file_ref> video_file{};
		std::optional<file_ref> info_file{};
	};

	vector<movie_data> load_from(fs::path const&, fs::path const&);
}  // namespace movies
