// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <filesystem>
#include <json/json.hpp>
#include <json/serdes.hpp>
#include <span>

#ifdef MOVIES_HAS_NAVIGATOR
#include <tangle/nav/navigator.hpp>
#endif

namespace movies {
	namespace fs = std::filesystem;

	struct alpha_2_aliases;

	enum class prefer_title { mine, theirs };

	template <typename Value>
	struct translatable {
		using map_t = std::map<std::string, Value>;
		using const_iterator = typename map_t::const_iterator;
		map_t items{};

		bool operator==(translatable const&) const noexcept = default;

		auto begin() const { return items.begin(); }
		auto end() const { return items.end(); }
		auto begin() { return items.begin(); }
		auto end() { return items.end(); }

		const_iterator fallback() const {
			std::string en[] = {"en-US"s, "en"s};
			return find(en);
		}

		const_iterator find(std::string_view lang) const {
			using namespace std::literals;
			while (!lang.empty()) {
				auto it = items.find({lang.data(), lang.size()});
				if (it != items.end()) return it;

				auto pos = lang.rfind('-');
				if (pos == std::string_view::npos) pos = 0;
				lang = lang.substr(0, pos);
			}
			return fallback();
		}

		const_iterator find(std::span<std::string const> langs) const {
			using namespace std::literals;
			bool has_en_US = false;
			bool has_en = false;
			for (auto const& lang : langs) {
				if (lang == "en-US"sv) has_en_US = true;
				if (lang == "en"sv) has_en = true;

				auto it = items.find(lang);
				if (it != items.end()) return it;
			}

			auto it = items.find({});
			if (it != items.end()) return it;
			if (!has_en_US || !has_en) return fallback();
			return items.end();
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
}  // namespace movies