// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

namespace movies::v1 {
	inline json::conv_result merge_one_prefixed(string_type& prev,
	                                            string_type const& next) {
		auto const changed = prev != next;
		prev = next;
		return changed ? json::conv_result::updated : json::conv_result::ok;
	}

	inline json::conv_result merge_one_prefixed(title_info& prev,
	                                            title_info const& next,
	                                            prefer_title which_title) {
		return prev.merge(next, which_title);
	}

	inline void merge_new_prefixed(std::string const& key,
	                               string_type const& next,
	                               translatable<string_type>& old_data,
	                               json::conv_result& result) {
		auto it = old_data.items.lower_bound(key);
		if (it != old_data.end() && it->first == key) return;
		result = json::conv_result::updated;
		old_data.items.insert(it, {key, next});
	}

	inline void merge_new_prefixed(std::string const& key,
	                               title_info const& next,
	                               translatable<title_info>& old_data,
	                               json::conv_result& result,
	                               prefer_title which_title) {
		auto it = old_data.items.lower_bound(key);
		if (it != old_data.end() && it->first == key) return;
		if (next.original) {
			auto cur = old_data.begin();
			for (auto end = old_data.end(); cur != end; ++cur) {
				if (cur->second.original) break;
			}

			if (cur != old_data.end()) {
				if (which_title == prefer_title::mine) return;
				old_data.items.erase(cur);
			}
		}
		result = json::conv_result::updated;
		old_data.items.insert(it, {key, next});
	}

	template <json::JsonStorableValue ValueType, typename... AdditionalArgs>
	inline json::conv_result merge_prefixed(
	    translatable<ValueType>& old_data,
	    translatable<ValueType> const& new_data,
	    AdditionalArgs... args) {
		auto result = json::conv_result::ok;
		for (auto& [key, prev] : old_data) {
			auto it = new_data.items.find(key);
			if (it == new_data.end()) continue;
			auto const res = merge_one_prefixed(prev, it->second, args...);
			if (!is_ok(res)) result = res;
			if (result == json::conv_result::failed) return result;
		}

		for (auto const& [key, next] : new_data) {
			merge_new_prefixed(key, next, old_data, result, args...);
		}

		return result;
	}

	template <MergableJsonValue Payload>
	inline json::conv_result merge(translatable<Payload>& old_data,
	                               translatable<Payload> const& new_data) {
		return merge_prefixed(old_data, new_data);
	}

	template <typename... Args, MergesObjectsWith<Args...> Payload>
	inline json::conv_result merge(translatable<Payload>& old_data,
	                               translatable<Payload> const& new_data,
	                               Args... args) {
		return merge_prefixed(old_data, new_data, args...);
	}
}  // namespace movies::v1
