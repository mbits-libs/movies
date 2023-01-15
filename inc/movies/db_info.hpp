// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <filesystem>
#include <movies/types.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace movies {
	struct alpha_2_aliases {
		std::unordered_set<string_type> codes{};
		std::unordered_map<string_type, string_type> aliases{};

		bool load(fs::path const& root);
		string_type map(string_type const&) const noexcept;

		static std::unordered_map<string_type, string_type> load_names(
		    fs::path const& root);
	};

	struct ref_renames {
		std::unordered_map<string_type, string_type> renames{};

		bool load(fs::path const& db_root, string_view_type domain);
		string_type map(string_type const&) const noexcept;
	};

	struct db_info {
		alpha_2_aliases aka{};
		ref_renames refs{};

		bool load(fs::path const& db_root, string_view_type domain);
		string_type map_aka(string_type const& key) const noexcept {
			return aka.map(key);
		}
		string_type map_ref(string_type const& key) const noexcept {
			return refs.map(key);
		}
	};
}  // namespace movies
