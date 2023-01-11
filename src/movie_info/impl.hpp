// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <movies/opt.hpp>
#include <span>

namespace movies::v1 {
	template <json::JsonStorableValue T>
	inline void store(json::map& dst,
	                  std::u8string_view const& key,
	                  T const& value) {
		return json::store(dst, key, value);
	}

	template <json::JsonStorableValue T>
	inline void store(json::map& dst,
	                  std::u8string_view const& key,
	                  std::optional<T> const& value) {
		return json::store(dst, key, value);
	}

	template <json::JsonStorableValue T>
	inline void store(json::map& dst,
	                  std::u8string_view const& key,
	                  std::vector<T> const& values) {
		return json::store(dst, key, values);
	}

	template <json::JsonStorableValue T>
	inline void store_or_value(json::map& dst,
	                           std::u8string_view const& key,
	                           std::vector<T> const& values) {
		return json::store(dst, key, values);
	}

	inline json::conv_result load(json::node const& src,
	                              json::LoadsJson auto& obj,
	                              std::string& dbg) {
		return json::load(src, obj, dbg);
	}

	inline json::conv_result load(json::node const& src,
	                              json::LoadsJsonRaw auto& obj,
	                              std::string& dbg) {
		return json::load(src, obj, dbg);
	}

	template <std::integral Int>
	inline json::conv_result load(json::node const& src,
	                              Int& val,
	                              std::string& dbg) {
		return json::load(src, val, dbg);
	}

	template <std::integral Int>
	inline json::conv_result load_zero(json::node const& src,
	                                   Int& val,
	                                   std::string& dbg) {
		return json::load_zero(src, val, dbg);
	}

	inline json::conv_result load(json::node const& src,
	                              bool& val,
	                              std::string& dbg) {
		return json::load(src, val, dbg);
	}

	inline json::conv_result load(json::node const& src,
	                              std::chrono::sys_seconds& val,
	                              std::string& dbg) {
		return json::load(src, val, dbg);
	}

	inline json::conv_result load_zero(json::node const& src,
	                                   std::chrono::sys_seconds& val,
	                                   std::string& dbg) {
		return json::load_zero(src, val, dbg);
	}

	inline json::conv_result load(json::node const& src,
	                              std::u8string& val,
	                              std::string& dbg) {
		return json::load(src, val, dbg);
	}

	template <json::LoadableJsonValue Payload>
	inline json::conv_result load(json::map const& src,
	                              std::u8string const& key,
	                              std::optional<Payload>& value,
	                              std::string& dbg) {
		return json::load(src, key, value, dbg);
	}

	template <json::LoadableJsonValue Payload>
	inline json::conv_result load_zero(json::map const& src,
	                                   std::u8string const& key,
	                                   std::optional<Payload>& value,
	                                   std::string& dbg) {
		return json::load_zero(src, key, value, dbg);
	}

	inline json::conv_result load(json::map const& src,
	                              std::u8string const& key,
	                              json::LoadableJsonValue auto& value,
	                              std::string& dbg) {
		return json::load(src, key, value, dbg);
	}

	inline json::conv_result load_zero(json::map const& src,
	                                   std::u8string const& key,
	                                   json::LoadableJsonValue auto& value,
	                                   std::string& dbg) {
		return json::load_zero(src, key, value, dbg);
	}

	template <json::LoadableJsonValue ValueType>
	inline json::conv_result load(json::array const& src,
	                              std::vector<ValueType>& value,
	                              std::string& dbg) {
		return json::load(src, value, dbg);
	}

	template <json::LoadableJsonValue ValueType>
	inline json::conv_result load(json::map const& src,
	                              std::u8string const& key,
	                              std::vector<ValueType>& value,
	                              std::string& dbg) {
		return json::load(src, key, value, dbg);
	}

	template <json::LoadableJsonValue ValueType>
	inline json::conv_result load_or_value(json::map const& src,
	                                       std::u8string const& key,
	                                       std::vector<ValueType>& value,
	                                       std::string& dbg) {
		return json::load_or_value(src, key, value, dbg);
	}

#if __cpp_lib_to_underlying >= 202102L
	using std::to_underlying;
#else
	template <class Enum>
	[[nodiscard]] constexpr std::underlying_type_t<Enum> to_underlying(
	    Enum _Value) noexcept {
		return static_cast<std::underlying_type_t<Enum> >(_Value);
	}
#endif

	template <typename Enum>
	concept is_enum = std::is_enum_v<Enum>;

	template <is_enum Enum>
	struct enum_traits;
	template <typename Final, is_enum Enum>
	struct enum_traits_helper {
		struct name_type {
			std::u8string_view name;
			Enum value;
		};

		static std::u8string_view name_for(Enum e) {
			for (auto const& [name, value] : Final::names()) {
				if (value == e) return name;
			}
			return {};
		}

		static Enum value_for(std::u8string_view n) {
			auto const span = Final::names();
			for (auto const& [name, value] : span) {
				if (name == n) return value;
			}
			return static_cast<Enum>(to_underlying(span.back().value) + 1);
		}
	};

	template <is_enum ValueType>
	inline void store(json::map& data,
	                  std::u8string_view key,
	                  ValueType value) {
		using traits = enum_traits<ValueType>;

		auto name = traits::name_for(value);
		if (name.empty()) return v1::store(data, key, to_underlying(value));
		return v1::store(data, key, std::u8string{name.data(), name.size()});
	}

	template <is_enum ValueType>
	inline json::conv_result load(json::map const& data,
	                              std::u8string const& key,
	                              ValueType& value,
	                              std::string& dbg) {
		using traits = enum_traits<ValueType>;

		auto result = json::conv_result::ok;
		auto json_str = json::cast<json::string>(data, key);
		auto json_int = json::cast<long long>(data, key);
		if (!json_str && !json_int) return json::conv_result::failed;

		if (json_str) {
			value = traits::value_for(*json_str);
			return json::conv_result::ok;
		}
		value = static_cast<ValueType>(*json_int);
		auto name = traits::name_for(value);
		if (!name.empty()) {
			dbg.append("\n- Numeric enum has a known name"sv);
			return json::conv_result::updated;
		}
		return json::conv_result::ok;
	}

	template <json::JsonStorableValue ValueType>
	inline json::conv_result store(json::map& data,
	                               std::u8string_view prefix,
	                               translatable<ValueType> const& value) {
		auto const as_s8v = [](std::string_view view) -> std::u8string_view {
			return {reinterpret_cast<char8_t const*>(view.data()), view.size()};
		};
		auto result = json::conv_result::ok;
		for (auto const& [key, item] : value.items) {
			if (key.empty()) {
				json::store(data, prefix, item);
				continue;
			}
			std::u8string full{};
			full.reserve(prefix.length() + key.length() + 1);
			full.append(prefix);
			full.push_back(':');
			full.append(as_s8v(key));
			json::store(data, full, item);
		}
		return result;
	}

#define LOAD_PREFIXED_ITEM()                                  \
	ValueType item{};                                         \
	auto const ret = json::load(data, key, item, dbg);        \
	if (!is_ok(ret)) result = ret;                            \
	if (result == ::json::conv_result::failed) return result; \
	if (ret == ::json::conv_result::opt) continue;

	template <json::LoadableJsonValue ValueType>
	inline json::conv_result load(json::map const& data,
	                              std::u8string_view prefix,
	                              translatable<ValueType>& value,
	                              std::string& dbg) {
		auto result = json::conv_result::ok;
		for (auto const& [key, _] : data) {
			if (!key.starts_with(prefix)) continue;
			if (key.length() == prefix.length()) {
				LOAD_PREFIXED_ITEM();
				value.items[{}] = std::move(item);
			} else if (key[prefix.length()] == u8':') {
				LOAD_PREFIXED_ITEM();
				auto const as_sv =
				    [](std::u8string_view view) -> std::string_view {
					return {reinterpret_cast<char const*>(view.data()),
					        view.size()};
				};
				auto view = as_sv(key).substr(prefix.length() + 1);
				value.items[{view.data(), view.size()}] = std::move(item);
			}
		}
		return result;
	}

	template <typename T>
	concept MergesObjects = requires(T& old_data, T const& new_data) {
		                        {
			                        old_data.merge(new_data)
			                        } -> std::convertible_to<json::conv_result>;
	                        };

	template <typename T, typename... Args>
	concept MergesObjectsWith =
	    requires(T& old_data, T const& new_data, Args... args) {
		    {
			    old_data.merge(new_data, args...)
			    } -> std::convertible_to<json::conv_result>;
	    };

	template <typename T>
	concept MergableJsonValueSimple =
	    std::integral<T> || is_enum<T> ||
	    std::same_as<T, std::chrono::sys_seconds> ||
	    std::same_as<T, std::u8string>;

	template <typename T>
	concept MergableJsonValue = MergesObjects<T> || MergableJsonValueSimple<T>;

	template <typename T, typename... Args>
	concept MergableJsonValueWith =
	    MergesObjectsWith<T, Args...> || MergableJsonValueSimple<T>;

	template <typename IntIsh>
	inline json::conv_result merge_int_ish(IntIsh& old_data, IntIsh new_data) {
		auto const prev = old_data;
		old_data = new_data;
		return prev != old_data ? json::conv_result::updated
		                        : json::conv_result::ok;
	}

	template <std::integral Int>
	inline json::conv_result merge(Int& old_data, Int new_data) {
		return merge_int_ish(old_data, new_data);
	}

	template <is_enum Enum>
	inline json::conv_result merge(Enum& old_data, Enum new_data) {
		return merge_int_ish(old_data, new_data);
	}

	inline json::conv_result merge(std::u8string& old_data,
	                               std::u8string const& new_data) {
		auto const changed = old_data != new_data;
		if (changed) old_data = new_data;
		return changed ? json::conv_result::updated : json::conv_result::ok;
	}

	template <typename... Args, MergesObjectsWith<Args...> ValueType>
	inline json::conv_result merge(ValueType& old_data,
	                               ValueType const& new_data,
	                               Args... args) {
		return old_data.merge(new_data, args...);
	}

	template <MergableJsonValue Payload>
	inline json::conv_result merge(std::optional<Payload>& old_data,
	                               std::optional<Payload> const& new_data) {
		auto const prev = old_data;
		old_data = new_data || old_data;
		return prev != old_data ? json::conv_result::updated
		                        : json::conv_result::ok;
	}
}  // namespace movies::v1

#define OP(CALL)                                                  \
	do {                                                          \
		auto const ret = (CALL);                                  \
		if (!is_ok(ret)) result = ret;                            \
		if (result == ::json::conv_result::failed) return result; \
	} while (0)

#include "impl_array.inl"
#include "impl_translatable.inl"