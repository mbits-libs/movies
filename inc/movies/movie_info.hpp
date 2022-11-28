// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <filesystem>
#include <json/json.hpp>
#include <json/serdes.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#ifdef MOVIES_HAS_NAVIGATOR
#include <tangle/nav/navigator.hpp>
#endif

namespace fs = std::filesystem;

namespace movies {
	struct alpha_2_aliases;

	enum class prefer_title { mine, theirs };

	template <typename Value>
	struct translatable {
		std::map<std::string, Value> items{};

		bool operator==(translatable const&) const noexcept = default;

		auto begin() const { return items.begin(); }
		auto end() const { return items.end(); }
		auto begin() { return items.begin(); }
		auto end() { return items.end(); }

		auto find(std::string_view lang) const {
			while (!lang.empty()) {
				auto it = items.find({lang.data(), lang.size()});
				if (it != items.end()) return it;

				auto pos = lang.rfind('-');
				if (pos == std::string_view::npos) pos = 0;
				lang = lang.substr(0, pos);
			}
			return items.find({});
		}

		template <typename Op>
		auto map(Op op) -> translatable<
		    std::remove_cvref_t<decltype(op(std::declval<Value>()))>> {
			translatable<
			    std::remove_cvref_t<decltype(op(std::declval<Value>()))>>
			    result{};
			for (auto const& [key, value] : items) {
				result.items[key] = op(value);
			}
			return result;
		}
	};

	struct title_info {
		std::u8string text;
		std::optional<std::u8string> sort;
		bool original{false};

		bool operator==(title_info const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::node const& data, std::string& dbg);
		json::conv_result merge(title_info const&, prefer_title);
		bool fixup(std::string& dbg);
	};

	struct role_info {
		long long id;
		std::optional<std::u8string> contribution;

		auto operator<=>(role_info const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::node const& data, std::string& dbg);
	};

	struct person_name {
		std::u8string name;
		std::vector<std::u8string> refs;

		auto operator<=>(person_name const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::node const& data, std::string& dbg);
	};

	struct crew_info {
		using role_list = std::vector<role_info>;
		role_list directors;
		role_list writers;
		role_list cast;

		bool operator==(crew_info const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::map const& data, std::string& dbg);
		json::conv_result rebuild(
		    json::map const& data,
		    std::string& dbg,
		    std::vector<person_name>& people,
		    std::map<std::u8string, std::u8string> const& old_refs);
		json::conv_result merge(crew_info const& new_data,
		                        std::vector<person_name>& people,
		                        std::vector<person_name> const& new_people);
	};

	struct poster_info {
		std::optional<std::u8string> small;
		std::optional<std::u8string> large;
		std::optional<std::u8string> normal;

		bool operator==(poster_info const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::map const& data, std::string& dbg);
		json::conv_result merge(poster_info const&);
	};

	struct image_info {
		std::optional<std::u8string> highlight;
		poster_info poster;
		std::vector<std::u8string> gallery;

		bool operator==(image_info const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::map const& data, std::string& dbg);
		json::conv_result merge(image_info const&);
	};

	struct dates_info {
		using opt_seconds = std::optional<std::chrono::sys_seconds>;
		opt_seconds published{};
		opt_seconds stream{};
		opt_seconds poster{};

		bool operator==(dates_info const&) const noexcept = default;

		json::node to_json() const;
		json::conv_result from_json(json::map const& data, std::string& dbg);
		json::conv_result merge(dates_info const&);

		static opt_seconds from_http_date(std::string const&);
	};

	struct movie_info {
		std::vector<std::u8string> refs;
		translatable<title_info> title;
		std::vector<std::u8string> genres;
		std::vector<std::u8string> countries;
		std::vector<std::u8string> age;
		std::vector<std::u8string> tags;
		std::vector<std::u8string> episodes;

		crew_info crew;
		std::vector<person_name> people;
		translatable<std::u8string> tagline;
		translatable<std::u8string> summary;
		image_info image;
		dates_info dates;
		std::optional<unsigned> year{}, runtime{}, rating{};

		bool operator==(movie_info const&) const noexcept = default;

		json::map to_json() const;
		json::conv_result load_crew(json::map const& data, std::string& dbg);
		json::conv_result load_old_title(json::map const& data,
		                                 std::string& dbg);
		json::conv_result from_json(json::map const& data, std::string& dbg);
		json::conv_result merge(movie_info const&, prefer_title);
		void add_tag(std::u8string_view tag);
		void remove_tag(std::u8string_view tag);
		bool has_tag(std::u8string_view tag) const noexcept;
		bool store(fs::path const& db_root, std::u8string_view dirname);
		json::conv_result load(fs::path const& db_root,
		                       std::u8string_view dirname,
		                       alpha_2_aliases const& aka,
		                       std::string& dbg);
		bool map_countries(alpha_2_aliases const& aka);

#ifdef MOVIES_HAS_NAVIGATOR
		bool offline_images(fs::path const& root,
		                    tangle::nav::navigator& nav,
		                    tangle::uri const& referrer,
		                    std::u8string_view dirname);
#endif
	};
}  // namespace movies
