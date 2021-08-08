// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <map>
#include <string>
#include <vector>
#include "fwd.hpp"

namespace movies {
	struct movie_info;

	struct diff {
		double ratio;
		string video, info;
		auto operator<=>(diff const&) const = default;
	};

	struct differ {
		map<string, movie_info> const& jsons;
		vector<string>& infos;
		vector<string>& videos;

		vector<diff> calc();
	};

}  // namespace movies
