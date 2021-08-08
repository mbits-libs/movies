// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include "fwd.hpp"
#include "movie_info.hpp"

namespace movies {
	struct movie_data {
		movie_info info{};
		std::optional<string> video_id{};
		std::optional<string> info_id{};
	};

	vector<movie_data> load_from(fs::path const&);
}  // namespace movies
