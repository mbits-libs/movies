// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <io/file.hpp>
#include <iostream>
#include <iterator>
#include <json/json.hpp>
#include <movies/db_info.hpp>
#include <movies/opt.hpp>

namespace movies {
	using namespace std::literals;

	bool alpha_2_aliases::load(fs::path const& db_root) {
		auto const json_filename = db_root / "iso-alpha-2.json"sv;

		auto const data = io::contents(json_filename);
		auto node = json::read_json({data.data(), data.size()});
		auto ptr = cast<json::map>(node);
		if (!ptr) return false;

		for (auto const& [code, aliases_node] : *ptr) {
			codes.insert(as_string_v(code));
			if (auto json_aliases = json::cast<json::array>(aliases_node)) {
				for (auto const& node : *json_aliases) {
					if (auto item = json::cast<json::string>(node)) {
						auto name = *item;
						for (auto& c : name)
							c = std::tolower(static_cast<unsigned char>(c));
						aliases[as_string(std::move(name))] = as_view(code);
					}
				}
			}
		}

		return true;
	}

	std::unordered_map<string_type, string_type> alpha_2_aliases::load_names(
	    fs::path const& db_root) {
		auto const json_filename = db_root / "iso-alpha-2.json"sv;

		std::unordered_map<string_type, string_type> result{};

		auto const data = io::contents(json_filename);
		auto node = json::read_json({data.data(), data.size()});
		auto ptr = cast<json::map>(node);
		if (!ptr) return result;

		for (auto const& [code, aliases_node] : *ptr) {
			if (auto json_aliases = json::cast<json::array>(aliases_node)) {
				if (json_aliases->empty()) continue;
				auto alias = json::cast<json::string>(json_aliases->front());
				if (alias && !alias->empty())
					result[as_string_v(code)] = as_view(*alias);
			}
		}

		return result;
	}

	string_type alpha_2_aliases::map(
	    string_type const& country) const noexcept {
		if (codes.count(country)) {
			return country;
		}

		auto lower = country;
		for (auto& c : lower)
			c = std::tolower(static_cast<unsigned char>(c));

		auto it = aliases.find(lower);
		if (it != aliases.end()) {
			return it->second;
		}

		return country;
	}

	bool ref_renames::load(fs::path const& db_root, string_view_type domain) {
		auto const json_filename = db_root / "refs.json"sv;

		auto const data = io::contents(json_filename);
		auto node = json::read_json({data.data(), data.size()});
		auto ptr = cast_from_json<json::map>(node, as_json_string_v(domain));
		if (!ptr) return false;

		for (auto const& [ref, json_rename] : *ptr) {
			if (auto rename = cast<json::string>(json_rename)) {
				renames[as_string_v(ref)] = as_string_v(*rename);
			}
		}

		return true;
	}

	string_type ref_renames::map(string_type const& ident) const noexcept {
		auto it = renames.find(ident);
		if (it == renames.end()) return ident;
		return it->second;
	}

	bool db_info::load(fs::path const& db_root, string_view_type domain) {
		if (!aka.load(db_root)) return false;
		return refs.load(db_root, domain);
	}
}  // namespace movies