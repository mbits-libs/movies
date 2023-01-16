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

#define SV_utf8(TEXT) u8##TEXT##sv
#define SV_ascii(TEXT) TEXT##sv

#define MOVIES_USE_U8STRING

namespace movies {
	namespace fs = std::filesystem;

#ifdef MOVIES_USE_U8STRING
	using string_type = std::u8string;
	using string_view_type = std::u8string_view;
#define SV SV_utf8
#else
	using string_type = std::string;
	using string_view_type = std::string_view;
#define SV SV_ascii
#endif

	using fs_string = std::u8string;
	using fs_string_view = std::u8string_view;

	template <typename Char>
	struct reverse;

	template <>
	struct reverse<char> {
		using char_t = char8_t;
		using string_t = std::u8string;
		using string_view_t = std::u8string_view;
	};

	template <>
	struct reverse<char8_t> {
		using char_t = char;
		using string_t = std::string;
		using string_view_t = std::string_view;
	};

	template <typename CharTo, typename CharFrom>
	inline std::basic_string_view<CharTo> view_conv(
	    std::basic_string_view<CharFrom> input) {
		if constexpr (std::same_as<CharFrom, CharTo>)
			return input;
		else if constexpr (sizeof(CharFrom) == sizeof(CharTo))
			return {
			    reinterpret_cast<CharTo const*>(input.data()),
			    input.size(),
			};
	}

	template <typename CharTo, typename CharFrom>
	inline std::basic_string<CharTo> string_conv(
	    std::basic_string_view<CharFrom> input) {
		if constexpr (std::same_as<CharFrom, CharTo>)
			return {input.data(), input.size()};
		else if constexpr (sizeof(CharFrom) == sizeof(CharTo))
			return {
			    reinterpret_cast<CharTo const*>(input.data()),
			    input.size(),
			};
	}

	template <typename CharTo, typename CharFrom>
	inline std::basic_string<CharTo> string_conv(
	    std::basic_string<CharFrom>&& input) {
		if constexpr (std::same_as<CharFrom, CharTo>)
			return input;
		else if constexpr (sizeof(CharFrom) == sizeof(CharTo))
			return {
			    reinterpret_cast<CharTo const*>(input.data()),
			    input.size(),
			};
	}

#define MOVIES_STR_SUITE(STR, VIEW, INFIX)                       \
	inline VIEW as_##INFIX##view(std::string_view input) {       \
		return view_conv<VIEW::value_type>(input);               \
	}                                                            \
	inline VIEW as_##INFIX##view(std::u8string_view input) {     \
		return view_conv<VIEW::value_type>(input);               \
	}                                                            \
	inline STR as_##INFIX##string_v(std::string_view input) {    \
		return string_conv<STR::value_type>(input);              \
	}                                                            \
	inline STR as_##INFIX##string_v(std::u8string_view input) {  \
		return string_conv<STR::value_type>(input);              \
	}                                                            \
	inline STR as_##INFIX##string(std::string&& input) {         \
		return string_conv<STR::value_type>(std::move(input));   \
	}                                                            \
	inline STR as_##INFIX##string(std::u8string&& input) {       \
		return string_conv<STR::value_type>(std::move(input));   \
	}                                                            \
	inline STR const& as_##INFIX##string_ref(STR const& input) { \
		return input;                                            \
	}                                                            \
	inline STR as_##INFIX##string_ref(                           \
	    reverse<STR::value_type>::string_view_t input) {         \
		auto view = view_conv<STR::value_type>(input);           \
		return {view.data(), view.size()};                       \
	}

	MOVIES_STR_SUITE(string_type, string_view_type, );
	MOVIES_STR_SUITE(std::string, std::string_view, ascii_);
	MOVIES_STR_SUITE(std::u8string, std::u8string_view, utf8_);
	MOVIES_STR_SUITE(fs_string, fs_string_view, fs_);
	MOVIES_STR_SUITE(json::string, json::string_view, json_);
#undef MOVIES_STR_SUITE

#define MOVIES_USING_STR_SUITE(DUMMY, INFIX) \
	using movies::as_##INFIX##view;          \
	using movies::as_##INFIX##string_v;      \
	using movies::as_##INFIX##string;        \
	using movies::as_##INFIX##string_ref

#define MOVIES_USING_STR                               \
	using string_type = movies::string_type;           \
	using string_view_type = movies::string_view_type; \
	using fs_string = movies::fs_string;               \
	using fs_string_view = movies::fs_string_view;     \
	MOVIES_USING_STR_SUITE(DUMMY, );                   \
	MOVIES_USING_STR_SUITE(DUMMY, ascii_);             \
	MOVIES_USING_STR_SUITE(DUMMY, utf8_);              \
	MOVIES_USING_STR_SUITE(DUMMY, fs_);                \
	MOVIES_USING_STR_SUITE(DUMMY, json_)

	struct alpha_2_aliases;

	template <typename Value>
	struct translatable {
		using map_t = std::map<std::string, Value>;
		using value_type = Value;
		using const_iterator = typename map_t::const_iterator;
		map_t items{};

		bool operator==(translatable const&) const noexcept = default;

		bool update(std::string const& key, Value const& value) {
			auto it = items.lower_bound(key);
			if (it == items.end() || it->first != key) {
				items.insert(it, {key, value});
				return true;
			}
			if (it->second != value) {
				it->second = value;
				return true;
			}
			return false;
		}

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