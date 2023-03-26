// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <map>
#include <string>
#include <vector>
#include "fwd.hpp"

namespace movies::v1 {

	struct diff {
		double ratio;
		std::u8string video, info;
		auto operator<=>(diff const&) const = default;
	};

	struct differ {
		map<std::u8string, movie_info> const& jsons;
		vector<std::u8string>& infos;
		vector<std::u8string>& videos;

		vector<diff> calc();
	};

}  // namespace movies::v1

namespace movies {
	using namespace v1;
}
