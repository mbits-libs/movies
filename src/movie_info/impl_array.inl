// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

namespace movies::v1 {
	template <typename Value>
	inline std::vector<std::pair<Value, size_t>> indexed(
	    std::vector<Value> const& values,
	    size_t offset = 0) {
		std::vector<std::pair<Value, size_t>> copy{};
		copy.reserve(values.size());
		std::transform(values.begin(), values.end(), std::back_inserter(copy),
		               [&offset](Value const& value) {
			               return std::pair{value, offset++};
		               });
		std::stable_sort(copy.begin(), copy.end());
		return copy;
	}

	template <typename Value>
	concept has_equiv = requires(Value const& old_data, Value const& new_data) {
		                    {
			                    old_data.equiv(new_data)
			                    } -> std::convertible_to<bool>;
	                    };
	static_assert(has_equiv<video_marker>);

	template <typename Value>
	inline bool equiv(Value const& old_data, Value const& new_data) {
		if constexpr (has_equiv<Value>)
			return old_data.equiv(new_data);
		else
			return old_data == new_data;
	}

	template <typename Expected, typename Value>
	static constexpr auto is_sized =
	    std::is_same_v<Value, std::pair<Expected, size_t>>;

	template <typename Value>
	inline Value const& select_equiv(Value const& old_data,
	                                        Value const& new_data) {
		if constexpr (is_sized<video_marker, Value>) {
			// keep old position
			if (old_data.first.comment == new_data.first.comment)
				return old_data;
			// update comment, move to new position
			return new_data;
		} else
			return old_data;
	}

	template <typename Value>
	inline json::conv_result merge_arrays(std::vector<Value>& old_data,
	                                      std::vector<Value> const& new_data) {
		auto old_ = indexed(old_data);
		auto new_ = indexed(new_data, old_.size());
		auto index_old = size_t{0};
		auto index_new = size_t{0};
		auto const size_old = old_.size();
		auto const size_new = new_.size();

		using mid_pair = std::pair<Value, size_t>;
		std::vector<mid_pair> midway{};
		while (index_old < size_old || index_new < size_new) {
			if (index_old == size_old) {
				midway.insert(midway.end(),
				              new_.begin() + static_cast<ptrdiff_t>(index_new),
				              new_.end());
				index_new = size_new;
				continue;
			}

			if (index_new == size_new) {
				midway.insert(midway.end(),
				              old_.begin() + static_cast<ptrdiff_t>(index_old),
				              old_.end());
				index_old = size_old;
				continue;
			}

			auto const& older = old_[index_old];
			auto const& newer = new_[index_new];

			if (equiv(older.first, newer.first)) {
				midway.push_back(select_equiv(older, newer));

				++index_old;
				++index_new;
				continue;
			}

			if (older.first < newer.first) {
				midway.push_back(older);
				++index_old;
				continue;
			}

			midway.push_back(newer);
			++index_new;
		}

		std::stable_sort(midway.begin(), midway.end(),
		                 [](auto const& lhs, auto const& rhs) -> bool {
			                 return lhs.second < rhs.second;
		                 });

		std::vector<Value> final{};
		final.reserve(midway.size());
		std::transform(std::make_move_iterator(midway.begin()),
		               std::make_move_iterator(midway.end()),
		               std::back_inserter(final),
		               [](mid_pair&& pair) { return std::move(pair.first); });

		if (old_data != final) {
			std::swap(old_data, final);
			return json::conv_result::updated;
		}

		return json::conv_result::ok;
	}

	template <typename Payload>
	inline json::conv_result merge(std::vector<Payload>& old_data,
	                               std::vector<Payload> const& new_data) {
		return merge_arrays(old_data, new_data);
	}
}  // namespace movies::v1
