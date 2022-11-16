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

		bool operator==(file_ref const&) const noexcept = default;
	};

	struct movie_data {
		movie_info info{};

		std::optional<file_ref> video_file{};
		std::optional<file_ref> info_file{};

		bool operator==(movie_data const&) const noexcept = default;
	};

	vector<movie_data> load_from(fs::path const&,
	                             fs::path const&,
	                             bool store_updates);
}  // namespace movies
