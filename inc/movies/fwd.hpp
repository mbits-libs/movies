// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <map>
#include <string>
#include <vector>

namespace movies {
	template <typename Key, typename Value, typename Compare = std::less<Key>>
	using map = std::map<Key, Value, Compare>;
	template <typename Value>
	using vector = std::vector<Value>;
}  // namespace movies
