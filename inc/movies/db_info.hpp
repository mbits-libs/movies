// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace movies {
	struct alpha_2_aliases {
		std::unordered_set<std::u8string> codes{};
		std::unordered_map<std::u8string, std::u8string> aliases{};

		bool load(fs::path const& root);
		std::u8string map(std::u8string const&) const noexcept;

		static std::unordered_map<std::u8string, std::u8string> load_names(
		    fs::path const& root);
	};

	struct ref_renames {
		std::unordered_map<std::u8string, std::u8string> renames{};

		bool load(fs::path const& db_root, std::u8string_view domain);
		std::u8string map(std::u8string const&) const noexcept;
	};

	struct db_info {
		alpha_2_aliases aka{};
		ref_renames refs{};

		bool load(fs::path const& db_root, std::u8string_view domain);
		std::u8string map_aka(std::u8string const& key) const noexcept {
			return aka.map(key);
		}
		std::u8string map_ref(std::u8string const& key) const noexcept {
			return refs.map(key);
		}
	};
}  // namespace movies
