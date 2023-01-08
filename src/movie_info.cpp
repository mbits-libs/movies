// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#ifdef _MSC_VER
#include <format>
#else
#include <date/date.h>
#include <fmt/format.h>
#endif
#include <execution>
#include <io/file.hpp>
#include <iostream>
#include <iterator>
#include <json/serdes.hpp>
#include <movies/db_info.hpp>
#include <movies/movie_info.hpp>
#include <movies/opt.hpp>
#include <numeric>
#include <set>
#include <utility>

#ifdef _MSC_VER
namespace fmt {
	using std::format;
}
#endif

namespace movies {
	namespace {
		using namespace std::literals;
#ifdef MOVIES_HAS_NAVIGATOR
		using namespace tangle;
#endif

#if __cpp_lib_to_underlying >= 202102L
		using std::to_underlying;
#else
		template <class Enum>
		[[nodiscard]] constexpr std::underlying_type_t<Enum> to_underlying(
		    Enum _Value) noexcept {
			return static_cast<std::underlying_type_t<Enum>>(_Value);
		}
#endif

		template <json::NodeType... Kind>
		inline std::tuple<Kind const*...> cast_from_json_multi(
		    json::map const& value,
		    json::string_view const& path) {
			auto node = from_json(value, path);
			return {cast<Kind>(node)...};
		}

		template <typename String>
		struct helper {
			static inline String as_str(std::u8string_view str) {
				return String{str.data(), str.size()};
			}
		};

		template <>
		struct helper<std::u8string_view> {
			static inline std::u8string_view as_str(
			    std::u8string_view str) noexcept {
				return str;
			}
		};

		size_t calc_separators(char sep, std::u8string_view data, size_t max) {
			auto pos = data.find(sep);
			decltype(pos) prev = 0;

			size_t result = 1;

			while (max && pos != std::u8string_view::npos) {
				prev = pos + 1;
				pos = data.find(sep, prev);
				if (max != std::u8string::npos) --max;

				++result;
			}

			return result;
		}

		template <typename String>
		std::vector<String> split_impl(char sep,
		                               std::u8string_view data,
		                               size_t max) {
			std::vector<String> result{};

			result.reserve(calc_separators(sep, data, max));

			auto pos = data.find(sep);
			decltype(pos) prev = 0;

			while (max && pos != std::u8string_view::npos) {
				auto const view = data.substr(prev, pos - prev);
				prev = pos + 1;
				pos = data.find(sep, prev);
				if (max != std::u8string::npos) --max;

				result.push_back(helper<String>::as_str(view));
			}

			result.push_back(helper<String>::as_str(data.substr(prev)));

			return result;
		}

		std::vector<std::u8string> split_s(char sep,
		                                   std::u8string_view data,
		                                   size_t max = std::u8string::npos) {
			return split_impl<std::u8string>(sep, data, max);
		}

		std::chrono::sys_seconds as_seconds(long long value) {
			return std::chrono::sys_seconds{std::chrono::seconds{value}};
		}
#define LOAD_PREFIXED_ITEM()                                  \
	ValueType item{};                                         \
	auto const ret = json::load(data, key, item, dbg);        \
	if (!is_ok(ret)) result = ret;                            \
	if (result == ::json::conv_result::failed) return result; \
	if (ret == ::json::conv_result::opt) continue;

		std::string_view as_sv(std::u8string_view view) {
			return {reinterpret_cast<char const*>(view.data()), view.size()};
		}

		std::u8string_view as_s8v(std::string_view view) {
			return {reinterpret_cast<char8_t const*>(view.data()), view.size()};
		}

		template <json::LoadableJsonValue ValueType>
		inline json::conv_result load_prefixed(json::map const& data,
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
					auto view = as_sv(key).substr(prefix.length() + 1);
					value.items[{view.data(), view.size()}] = std::move(item);
				}
			}
			return result;
		}

		json::conv_result merge_one_prefixed(std::u8string& prev,
		                                     std::u8string const& next) {
			auto const changed = prev != next;
			prev = next;
			return changed ? json::conv_result::updated : json::conv_result::ok;
		}

		json::conv_result merge_one_prefixed(title_info& prev,
		                                     title_info const& next,
		                                     prefer_title which_title) {
			return prev.merge(next, which_title);
		}

		void merge_new_prefixed(std::string const& key,
		                        std::u8string const& next,
		                        translatable<std::u8string>& old_data,
		                        json::conv_result& result) {
			auto it = old_data.items.lower_bound(key);
			if (it != old_data.end() && it->first == key) return;
			result = json::conv_result::updated;
			old_data.items.insert(it, {key, next});
		}

		void merge_new_prefixed(std::string const& key,
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
		json::conv_result merge_prefixed(
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

		template <json::JsonStorableValue ValueType>
		inline json::conv_result store_prefixed(
		    json::map& data,
		    std::u8string_view prefix,
		    translatable<ValueType> const& value) {
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

		json::conv_result array_from_json(json::array const* input,
		                                  std::vector<role_info>& output,
		                                  std::string& dbg) {
			if (!input) return json::conv_result::ok;

			output.clear();
			output.reserve(input->size());

			auto result = json::conv_result::ok;
			for (auto const& node : *input) {
				output.emplace_back();
				role_info info{};
				auto const res = output.back().from_json(node, dbg);
				if (!is_ok(res)) result = res;
				if (result == json::conv_result::failed) break;
			}
			return result;
		}

		json::conv_result array_from_json(json::array const* input,
		                                  std::vector<std::u8string>& output,
		                                  std::string&) {
			if (!input) return json::conv_result::ok;

			output.clear();
			output.reserve(input->size());

			auto result = json::conv_result::ok;
			for (auto const& node : *input) {
				auto str = cast<std::u8string>(node);
				if (!str) continue;
				output.emplace_back(*str);
			}
			return result;
		}

		json::conv_result array_from_json(
		    std::tuple<std::u8string const*, json::array const*> const& input,
		    std::vector<std::u8string>& output,
		    std::string& dbg) {
			auto const& str = std::get<0>(input);
			if (str) {
				output.clear();
				if (str->empty()) {
					dbg.append("\n- Empty string could be a non-array"sv);
					return json::conv_result::updated;
				}
				output.push_back(*str);
				return json::conv_result::ok;
			}
			return array_from_json(std::get<1>(input), output, dbg);
		}

		json::conv_result map_from_json(
		    json::map const* input,
		    std::map<std::u8string, std::u8string>& output,
		    std::string&) {
			if (!input) return json::conv_result::ok;

			output.clear();

			auto result = json::conv_result::ok;
			for (auto const& [key, value] : *input) {
				auto fld = cast<std::u8string>(value);
				if (!fld || fld->empty()) {
					result = json::conv_result::updated;
				} else {
					output[key] = *fld;
				}
			}
			return result;
		}

		template <typename Value>
		std::vector<std::pair<Value, size_t>> indexed(
		    std::vector<Value> const& values,
		    size_t offset = 0) {
			std::vector<std::pair<Value, size_t>> copy{};
			copy.reserve(values.size());
			std::transform(values.begin(), values.end(),
			               std::back_inserter(copy),
			               [&offset](Value const& value) {
				               return std::pair{value, offset++};
			               });
			std::stable_sort(copy.begin(), copy.end());
			return copy;
		}

		template <typename Value>
		concept has_equiv = requires(Value const& old_data,
		                             Value const& new_data) {
			{ old_data.equiv(new_data) } -> std::convertible_to<bool>;
		};
		template <typename Value>
		bool equiv(Value const& old_data, Value const& new_data) {
			if constexpr (has_equiv<Value>)
				return old_data.equiv(new_data);
			else
				return old_data == new_data;
		}

		template <typename Expected, typename Value>
		static constexpr auto is_sized =
		    std::is_same_v<Value, std::pair<Expected, size_t>>;
		template <typename Value>
		Value const& select_equiv(Value const& old_data,
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
		json::conv_result merge_arrays(std::vector<Value>& old_data,
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
					midway.insert(
					    midway.end(),
					    new_.begin() + static_cast<ptrdiff_t>(index_new),
					    new_.end());
					index_new = size_new;
					continue;
				}

				if (index_new == size_new) {
					midway.insert(
					    midway.end(),
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
			               std::back_inserter(final), [](mid_pair&& pair) {
				               return std::move(pair.first);
			               });

			if (old_data != final) {
				std::swap(old_data, final);
				return json::conv_result::updated;
			}

			return json::conv_result::ok;
		}

		struct marker_type_str {
			marker_type type;
			std::u8string_view name;
		};
		static constexpr marker_type_str marker_types[] = {
#define X_MARKER_TYPE(NAME) {marker_type::NAME, u8## #NAME##sv},
		    VIDEO_MARKER_TYPE_X(X_MARKER_TYPE)
#undef X_MARKER_TYPE
		};

		constexpr std::u8string_view marker_name(marker_type type) {
			for (auto const& marker : marker_types)
				if (marker.type == type) return marker.name;
			return {};
		}

		constexpr marker_type marker_from_name(std::u8string_view name) {
			for (auto const& marker : marker_types)
				if (marker.name == name) return marker.type;
			return static_cast<marker_type>(
			    to_underlying(marker_types[std::size(marker_types) - 1].type) +
			    1);
		}

#define TWC(fld) \
	if (auto cmp = lhs.fld <=> rhs.fld; cmp != 0) return cmp < 0

		struct person_info_t {
			std::optional<std::u8string> name;
			std::map<std::u8string, std::u8string> refs;
			std::optional<std::u8string> contribution;
			size_t original_position{};
			bool from_existing{};

			person_info_t merge(person_info_t const& new_data) && {
				// names must be equal
				for (auto const& [key, value] : new_data.refs) {
					auto it = refs.lower_bound(key);
					if (it != refs.end() && it->first == key) continue;
					refs.insert(it, {key, value});
				}
				contribution = new_data.contribution || contribution;
				return std::move(*this);
			}

			long long add_to(std::vector<person_name>& used) const {
				if (!name) {
					return (std::numeric_limits<long long>::max)();
				}
				std::vector<std::u8string> reflist{};
				reflist.reserve(refs.size());
				for (auto const& [key, value] : refs) {
					if (value.empty()) {
						reflist.push_back(key);
					} else {
						std::u8string ref{};
						ref.reserve(key.length() + value.length() + 1);
						ref.append(key);
						ref.push_back(':');
						ref.append(value);
						reflist.push_back(ref);
					}
				}

				long long index{};
				for (auto const& info : used) {
					if (info.name == *name && info.refs == reflist)
						return index;
					++index;
				}

				used.push_back({*name, std::move(reflist)});
				return index;
			}

			static auto find_in(std::vector<person_name> const& list,
			                    long long index) {
				if (index < 0) return list.end();
				auto const uindex = static_cast<size_t>(index);
				return uindex >= list.size()
				           ? list.end()
				           : std::next(list.begin(),
				                       static_cast<ptrdiff_t>(uindex));
			}

			static std::vector<person_info_t> conv(
			    crew_info::role_list const& list,
			    std::vector<person_name> const& names,
			    bool from_existing) {
				std::vector<person_info_t> result{};
				result.resize(list.size());
				for (size_t index = 0; index < list.size(); ++index) {
					auto& dst = result[index];
					auto const& src = list[index];
					auto it = find_in(names, src.id);

					if (it != names.end()) {
						dst.name = it->name;
						for (auto const& ref : it->refs) {
							auto const view = std::u8string_view{ref};
							auto const pos = ref.find(':');
							if (pos == std::u8string::npos) {
								dst.refs[ref].clear();
							} else {
								dst.refs[ref.substr(0, pos)] =
								    ref.substr(pos + 1);
							}
						}
					}

					dst.contribution = src.contribution;
					dst.original_position = index;
					dst.from_existing = from_existing;
				}
				std::erase_if(result, [](person_info_t const& person) {
					return !person.name;
				});

				std::stable_sort(
				    result.begin(), result.end(),
				    [](person_info_t const& lhs,
				       person_info_t const& rhs) -> bool {
					    TWC(name);
					    TWC(contribution);
					    TWC(refs);
					    if (lhs.original_position != rhs.original_position)
						    return lhs.original_position <
						           rhs.original_position;
					    if (lhs.from_existing == rhs.from_existing)
						    return false;
					    return !lhs.from_existing;
				    });
				return result;
			}

			static std::vector<person_info_t> merge(
			    std::vector<person_info_t> const& old_data,
			    std::vector<person_info_t> const& new_data) {
				auto index_old = size_t{0};
				auto index_new = size_t{0};
				auto const size_old = old_data.size();
				auto const size_new = new_data.size();

				std::vector<person_info_t> result{};
				while (index_old < size_old || index_new < size_new) {
					if (index_old == size_old) {
						result.insert(result.end(),
						              new_data.begin() +
						                  static_cast<ptrdiff_t>(index_new),
						              new_data.end());
						index_new = size_new;
						continue;
					}

					if (index_new == size_new) {
						result.insert(result.end(),
						              old_data.begin() +
						                  static_cast<ptrdiff_t>(index_old),
						              old_data.end());
						index_old = size_old;
						continue;
					}

					auto const& older = old_data[index_old];
					auto const& newer = new_data[index_new];

					if (older.name == newer.name) {
						auto const compatible_contribution =
						    (older.contribution == newer.contribution) ||
						    (older.contribution && !newer.contribution);

						if (compatible_contribution) {
							result.push_back(person_info_t{older}.merge(newer));
						} else {
							result.push_back(older);
							result.push_back(newer);
						}

						++index_old;
						++index_new;
						continue;
					}

					if (older.name < newer.name) {
						result.push_back(older);
						++index_old;
						continue;
					}

					result.push_back(newer);
					++index_new;
				}

				std::stable_sort(
				    result.begin(), result.end(),
				    [](person_info_t const& lhs,
				       person_info_t const& rhs) -> bool {
					    if (lhs.from_existing != rhs.from_existing)
						    return !lhs.from_existing;
					    if (lhs.original_position != rhs.original_position)
						    return lhs.original_position <
						           rhs.original_position;
					    TWC(name);
					    TWC(contribution);
					    TWC(refs);
					    return false;
				    });

				return result;
			}
		};

#undef TWC

#ifdef MOVIES_HAS_NAVIGATOR
		std::u8string as_u8(std::string_view utf8byAnotherName) {
			return {reinterpret_cast<char8_t const*>(utf8byAnotherName.data()),
			        utf8byAnotherName.size()};
		}

		std::string as_platform(std::u8string_view utf) {
			return {reinterpret_cast<char const*>(utf.data()), utf.size()};
		}

		void rebase_img(std::optional<std::u8string>& dst,
		                uri const& base,
		                std::map<std::u8string, uri>& mapping,
		                std::u8string_view dirname,
		                std::u8string_view filename) {
			if (dst && dst->empty()) dst = std::nullopt;
			if (!dst) return;

			auto src = uri::canonical(uri{as_platform(*dst)}, base);
			auto ext = fs::path{src.path()}.extension().u8string();
			if (ext.empty() || ext == u8".jpeg"sv) ext = u8".jpg"sv;
			std::u8string result{};
			result.reserve(dirname.length() + filename.length() + ext.length() +
			               1);
			result.append(dirname);
			result.push_back('/');
			result.append(filename);
			result.append(ext);
			dst = std::move(result);
			mapping[*dst] = src;
		}

		std::map<std::u8string, uri> rebase_all(movies::image_info& self,
		                                        std::u8string_view dirname,
		                                        uri const& base) {
			std::map<std::u8string, uri> mapping;

			rebase_img(self.highlight, base, mapping, dirname,
			           u8"01-highlight"sv);
			rebase_img(self.poster.small, base, mapping, dirname,
			           u8"00-poster-0_small"sv);
			rebase_img(self.poster.normal, base, mapping, dirname,
			           u8"00-poster-1_normal"sv);
			rebase_img(self.poster.large, base, mapping, dirname,
			           u8"00-poster-2_large"sv);

			size_t index{};
			for (auto& image : self.gallery) {
				std::optional<std::u8string> sure{std::move(image)};
				rebase_img(sure, base, mapping, dirname,
				           as_u8(fmt::format("02-gallery-{:02}"sv, index++)));
				if (sure) image = std::move(*sure);
			}

			return mapping;
		}

		inline nav::request prepareReq(uri const& address,
		                               uri const& referrer) {
			nav::request req{address};
			if (!referrer.empty()) req.referrer(referrer);
			return req;
		}
#endif
	}  // namespace

#define JSON_CAST(field_name, type) \
	auto json_##field_name = cast_from_json<type>(data, u8## #field_name##sv)

#define JSON_MAP_COPY(field_name)                                  \
	{                                                              \
		if (auto const res =                                       \
		        map_from_json(json_##field_name, field_name, dbg); \
		    res != json::conv_result::ok) {                        \
			result = res;                                          \
			if (res == json::conv_result::updated)                 \
				dbg.append(" (" #field_name ")"sv);                \
		}                                                          \
		if (result == json::conv_result::failed) return result;    \
	}

#define LOAD_PREFIXED(field_name) \
	LOAD_EX(load_prefixed(data, NAMED_SV(field_name), dbg))

#define STORE_PREFIXED(field_name) store_prefixed(result, NAMED_SV(field_name))

#define MERGE(field_name)                        \
	auto const prev_##field_name = (field_name); \
	(field_name) = new_data.field_name

#define CHANGED(field_name) (prev_##field_name != (field_name))

#ifdef _MSC_VER
#define MERGE_BEGIN(TYPE, ...)                                         \
	json::conv_result TYPE::merge(TYPE const& new_data, __VA_ARGS__) { \
		auto result = json::conv_result::ok;                           \
		do {
#else
#define MERGE_BEGIN(TYPE, ...)                                        \
	json::conv_result TYPE::merge(TYPE const& new_data __VA_OPT__(, ) \
	                                  __VA_ARGS__) {                  \
		auto result = json::conv_result::ok;                          \
		do {
#endif

#define MERGE_END() \
	}               \
	while (0)       \
		;           \
	return result;  \
	}

#define MERGE_VAL(field_name)                                         \
	{                                                                 \
		MERGE(field_name);                                            \
		if (CHANGED(field_name)) result = json::conv_result::updated; \
	}

#define MERGE_OPT(field_name)                                         \
	{                                                                 \
		auto const prev_##field_name = field_name;                    \
		(field_name) = new_data.field_name || (field_name);           \
		if (CHANGED(field_name)) result = json::conv_result::updated; \
	}

#ifdef _MSC_VER
#define MERGE_OBJ(field_name, ...)                                    \
	{                                                                 \
		auto const sub_result =                                       \
		    field_name.merge(new_data.field_name, __VA_ARGS__);       \
		if (sub_result != json::conv_result::ok) result = sub_result; \
		if (result == json::conv_result::failed) break;               \
	}
#define MERGE_PREFIXED(field_name, ...)                                   \
	{                                                                     \
		auto const sub_result =                                           \
		    merge_prefixed(field_name, new_data.field_name, __VA_ARGS__); \
		if (sub_result != json::conv_result::ok) result = sub_result;     \
		if (result == json::conv_result::failed) break;                   \
	}
#else
#define MERGE_OBJ(field_name, ...)                                            \
	{                                                                         \
		auto const sub_result =                                               \
		    field_name.merge(new_data.field_name __VA_OPT__(, ) __VA_ARGS__); \
		if (sub_result != json::conv_result::ok) result = sub_result;         \
		if (result == json::conv_result::failed) break;                       \
	}
#define MERGE_PREFIXED(field_name, ...)                                  \
	{                                                                    \
		auto const sub_result = merge_prefixed(                          \
		    field_name, new_data.field_name __VA_OPT__(, ) __VA_ARGS__); \
		if (sub_result != json::conv_result::ok) result = sub_result;    \
		if (result == json::conv_result::failed) break;                  \
	}
#endif

#define MERGE_ARRAY(field_name)                                                \
	{                                                                          \
		auto const sub_result = merge_arrays(field_name, new_data.field_name); \
		if (sub_result != json::conv_result::ok) result = sub_result;          \
		if (result == json::conv_result::failed) break;                        \
	}

#define MERGE_MAP(field_name)                                                \
	{                                                                        \
		auto const sub_result = merge_maps(field_name, new_data.field_name); \
		if (sub_result != json::conv_result::ok) result = sub_result;        \
		if (result == json::conv_result::failed) break;                      \
	}

	struct old_title_info {
		std::optional<std::u8string> local;
		std::optional<std::u8string> orig;
		std::optional<std::u8string> sort;

		bool operator==(old_title_info const&) const noexcept = default;

		json::conv_result from_json(json::map const& data, std::string& dbg);
	};

	json::conv_result old_title_info::from_json(json::map const& data,
	                                            std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD(local);
		LOAD(orig);
		LOAD(sort);

		return result;
	}

	json::node title_info::to_json() const {
		std::vector<std::pair<json::string, json::node>> values;
		values.reserve(3);
		values.emplace_back(u8"text", text);
		if (sort) values.emplace_back(u8"sort", *sort);
		if (original) values.emplace_back(u8"original", true);

		if (values.empty()) return {};
		if (values.size() == 1) return std::move(values.front().second);
		return json::map{std::make_move_iterator(values.begin()),
		                 std::make_move_iterator(values.end())};
	}

	json::conv_result title_info::from_json(json::node const& data,
	                                        std::string& dbg) {
		auto result = json::conv_result::ok;
		auto json_text = cast<json::string>(data);
		auto map = cast<json::map>(data);

		if (json_text) {
			text = *json_text;
			sort = std::nullopt;
			original = false;
		} else if (map) {
			auto const& data = *map;
			auto local = cast<json::string>(data, u8"local"s);
			auto orig = cast<json::string>(data, u8"orig"s);
			if (local || orig) {
				return json::conv_result::opt;
			}
			std::optional<std::u8string> text;
			LOAD(text);
			LOAD(sort);
			LOAD(original);
			if (!text) return json::conv_result::opt;
			this->text = *text;
		} else {
			return json::conv_result::opt;
		}

		return fixup(dbg) ? json::conv_result::updated : result;
	}

	json::conv_result title_info::merge(title_info const& new_data,
	                                    prefer_title which_title) {
		auto const prev_text = text;
		auto const prev_sort = sort;
		auto const prev_original = original;

		if (which_title == prefer_title::theirs) {
			text = new_data.text;
			sort = new_data.sort || sort;
			original = new_data.original;
		} else {
			if (text.empty()) text = new_data.text;
			if (!sort) sort = new_data.sort;
		}

		std::string ignore{};
		fixup(ignore);

		return CHANGED(text) || CHANGED(sort) || CHANGED(original)
		           ? json::conv_result::updated
		           : json::conv_result::ok;
	}

	bool title_info::fixup(std::string& dbg) {
		bool changed = false;

		if (text.empty() && sort) text = *sort;
		if (sort && text == *sort) sort = std::nullopt;

		if (sort && sort->empty()) {
			sort = std::nullopt;
			dbg.append("\n- Empty sort title string found"sv);
			changed = true;
		}

		return changed;
	}

	json::node role_info::to_json() const {
		if (!contribution) return id;
		return json::array{id, *contribution};
	}

	json::conv_result role_info::from_json(json::node const& data,
	                                       std::string&) {
		auto ref = cast<long long>(data);
		auto arr = cast<json::array>(data);

		if (ref) {
			id = *ref;
			contribution = std::nullopt;
			return json::conv_result::ok;
		}

		if (arr && arr->size() == 2) {
			auto ref = json::cast<long long>(arr->front());
			auto contrib = json::cast<std::u8string>(arr->back());
			if (ref && contrib) {
				id = *ref;
				if (contrib->empty())
					contribution = std::nullopt;
				else
					contribution = *contrib;
				return json::conv_result::ok;
			}
		}

		return json::conv_result::failed;
	}

	json::node person_name::to_json() const {
		if (refs.empty()) return name;
		json::array result{};
		result.reserve(refs.size() + 1);
		result.push_back(name);
		std::transform(refs.begin(), refs.end(), std::back_inserter(result),
		               [](auto const& item) { return item; });
		return json::node{std::move(result)};
	}

	json::conv_result person_name::from_json(json::node const& data,
	                                         std::string&) {
		auto str = cast<std::u8string>(data);
		auto arr = cast<json::array>(data);

		if (str) {
			name = *str;
			refs.clear();
			return json::conv_result::ok;
		}

		if (arr) {
			if (!arr->empty()) refs.reserve(arr->size());
			bool name_set = false;
			for (auto const& item : *arr) {
				auto val = cast<std::u8string>(item);
				if (!val) continue;
				if (!name_set) {
					name_set = true;
					name = *val;
					continue;
				}
				refs.push_back(*val);
			}
			if (name_set) return json::conv_result::ok;
		}

		return json::conv_result::failed;
	}

	json::node crew_info::to_json() const {
		json::map result{};
		STORE(directors);
		STORE(writers);
		STORE(cast);
		STORE(names);
		if (result.empty()) return {};
		return result;
	}

	json::conv_result crew_info::from_json_short(json::map const& data,
	                                             std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD(directors);
		LOAD(writers);
		LOAD(cast);
		LOAD_EX(cleanup_names(dbg));
		return result;
	}

	json::conv_result crew_info::from_json(json::map const& data,
	                                       std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD(names);
		LOAD_EX(from_json_short(data, dbg));
		return result;
	}

	json::conv_result crew_info::cleanup_names(std::string& dbg) {
		long long id{};
		for (auto const& person : names) {
			if (person.name == u8"N/A"sv) {
				break;
			}
			++id;
		}

		if (static_cast<size_t>(id) < names.size()) {
			role_list* lists[] = {&directors, &writers, &cast};

			for (auto const& list_ptr : lists) {
				auto& list = *list_ptr;
				auto it = std::find_if(
				    list.begin(), list.end(),
				    [id](role_info const& role) { return role.id == id; });
				if (it != list.end()) list.erase(it);
				for (auto& role : list) {
					if (role.id > id) --role.id;
				}
			}

			names.erase(std::next(names.begin(), id));
			dbg.append("\n- N/A found in the name list"sv);
			return json::conv_result::updated;
		}

		return json::conv_result::ok;
	}

	void add_person(std::u8string const& ref,
	                std::optional<std::u8string> const& contribution,
	                std::vector<person_name>& names,
	                std::map<std::u8string, std::u8string> const& old_refs,
	                std::map<std::u8string, long long>& re_ref,
	                crew_info::role_list& dst) {
		auto it = old_refs.find(ref);
		if (it != old_refs.end()) {
			auto it2 = re_ref.find(ref);
			if (it2 != re_ref.end()) {
				dst.push_back({it2->second, contribution});
				return;
			}

			auto const id = static_cast<long long>(names.size());
			names.push_back({it->second, {ref}});
			dst.push_back({id, contribution});
			re_ref[ref] = id;
			return;
		}

		auto const id = static_cast<long long>(names.size());
		names.push_back({ref, {}});
		dst.push_back({id, contribution});
	}

	json::conv_result crew_info::rebuild(
	    json::map const& data,
	    std::string& dbg,
	    std::map<std::u8string, std::u8string> const& old_refs) {
		std::map<std::u8string, long long> re_ref;

		std::pair<role_list*, std::u8string> lists[] = {
		    {&directors, u8"directors"s},
		    {&writers, u8"writers"s},
		    {&cast, u8"cast"s},
		};
		auto result = json::conv_result::ok;

		for (auto const& [list_ptr, name] : lists) {
			auto& list = *list_ptr;
			list.clear();
			auto items = json::cast<json::array>(data, name);
			if (!items) continue;

			for (auto const& item : *items) {
				auto ref = json::cast<long long>(item);
				auto str = json::cast<std::u8string>(item);
				auto arr = json::cast<json::array>(item);

				if (ref) {
					list.push_back({*ref, std::nullopt});
					continue;
				}

				if (str) {
					add_person(*str, std::nullopt, names, old_refs, re_ref,
					           list);
					result = json::conv_result::updated;
					continue;
				}

				if (arr && arr->size() == 2) {
					auto ref = json::cast<long long>(arr->front());
					auto str = json::cast<std::u8string>(arr->front());
					auto contrib = json::cast<std::u8string>(arr->back());

					std::optional<std::u8string> val;
					if (contrib && !contrib->empty()) val = *contrib;

					if (ref && contrib) {
						list.push_back({*ref, val});
						continue;
					}
					if (str && contrib) {
						add_person(*str, val, names, old_refs, re_ref, list);
						result = json::conv_result::updated;
						continue;
					}
				}

				return json::conv_result::failed;
			}
		}

		if (result == json::conv_result::updated) {
			dbg.append("\n- Cast & crew needs rewiring"sv);
		}

		return result;
	}

	json::conv_result crew_info::merge(crew_info const& new_data) {
		auto result = json::conv_result::ok;
		std::vector<person_name> used{};
		for (auto list : {
		         &crew_info::directors,
		         &crew_info::writers,
		         &crew_info::cast,
		     }) {
			auto new_list = person_info_t::merge(
			    person_info_t::conv(this->*list, names, true),
			    person_info_t::conv(new_data.*list, new_data.names, false));

			role_list copied{};
			copied.reserve(new_list.size());
			for (auto& item : new_list) {
				auto const id = item.add_to(used);
				copied.push_back(role_info{id, std::move(item.contribution)});
			}

			if (copied != this->*list) {
				std::swap(copied, this->*list);
				if (result == json::conv_result::ok)
					result = json::conv_result::updated;
			}
		}

		if (used != names) {
			std::swap(used, names);
			if (result == json::conv_result::ok)
				result = json::conv_result::updated;
		}

		return result;
	}

	json::node poster_info::to_json() const {
		json::map result{};
		STORE(small);
		STORE(large);
		STORE(normal);
		if (result.empty()) return {};
		return result;
	}

	json::conv_result poster_info::from_json(json::map const& data,
	                                         std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD(small);
		LOAD(large);
		LOAD(normal);

		return result;
	}

	MERGE_BEGIN(poster_info)
	MERGE_OPT(small);
	MERGE_OPT(large);
	MERGE_OPT(normal);
	MERGE_END()

	json::node image_info::to_json() const {
		json::map result{};
		STORE(highlight);
		STORE(poster);
		STORE(gallery);
		if (result.empty()) return {};
		return result;
	}

	json::conv_result image_info::from_json(json::map const& data,
	                                        std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD_EX(json::load(data, NAMED(highlight), dbg));
		LOAD(poster);
		LOAD(gallery);
		return result;
	}

	MERGE_BEGIN(image_info)
	MERGE_OPT(highlight);
	MERGE_OBJ(poster);
	MERGE_ARRAY(gallery);
	MERGE_END()

	json::node dates_info::to_json() const {
		json::map result{};
		STORE(published);
		STORE(stream);
		STORE(poster);
		if (result.empty()) return {};
		return result;
	}

	json::conv_result dates_info::from_json(json::map const& data,
	                                        std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD(published);
		LOAD(stream);
		LOAD(poster);
		return result;
	}

	MERGE_BEGIN(dates_info)
	MERGE_OPT(published);
	MERGE_OPT(stream);
	MERGE_OPT(poster);
	MERGE_END()

	dates_info::opt_seconds dates_info::from_http_date(
	    std::string const& header) {
		for (auto format : {
		         "%a, %d %b %Y %T GMT",
		         "%A, %d-%b-%y %T GMT",
		     }) {
			std::istringstream in(header);
			std::chrono::sys_seconds result{};
#ifdef _MSC_VER
			in >> std::chrono::parse(std::string{format}, result);
#else
			in >> date::parse(std::string{format}, result);
#endif
			if (!in.fail()) return result;
		}

		return std::nullopt;
	}

	json::node video_marker::to_json() const {
		json::map result{};
		auto name = marker_name(type);
		if (name.empty())
			::json::store(result, u8"type"sv, to_underlying(type));
		else
			::json::store(result, u8"type"sv,
			              json::string{name.data(), name.size()});
		STORE(start);
		STORE(stop);
		STORE(comment);
		return result;
	}

	json::conv_result video_marker::from_json(json::map const& data,
	                                          std::string& dbg) {
		auto result = json::conv_result::ok;
		auto json_type_str = json::cast<json::string>(data, u8"type");
		auto json_type_int = json::cast<long long>(data, u8"type");
		if (!json_type_str && !json_type_int) {
			result = json::conv_result::failed;
			return result;
		}
		if (json_type_str) {
			type = marker_from_name(*json_type_str);
		} else {
			type = static_cast<marker_type>(*json_type_int);
			auto name = marker_name(type);
			if (!name.empty()) {
				dbg.append("\n- Numeric marker type has a known name"sv);
				result = json::conv_result::updated;
			}
		}
		LOAD_EX(::json::load_zero(data, NAMED(start), dbg));
		LOAD(stop);
		LOAD(comment);
		return result;
	}

	json::node video_info::to_json() const {
		json::map result{};
		STORE(credits);
		STORE(end_of_watch);
		STORE(markers);
		if (result.empty()) return {};
		return result;
	}

	json::conv_result video_info::from_json(json::map const& data,
	                                        std::string& dbg) {
		auto result = json::conv_result::ok;
		LOAD(credits);
		LOAD(end_of_watch);
		LOAD(markers);
		return result;
	}

	MERGE_BEGIN(video_info)
	MERGE_OPT(credits);
	MERGE_OPT(end_of_watch);
	MERGE_ARRAY(markers);
	MERGE_END()

	void store_tr_strings(
	    json::map& dst,
	    std::u8string_view const& prefix,
	    std::map<std::u8string, std::u8string> const& values) {
		for (auto const& [key, value] : values) {
			if (key.empty()) {
				dst[{prefix.data(), prefix.size()}] = value;
			} else {
				std::u8string full{};
				full.reserve(prefix.length() + key.length() + 1);
				full.append(prefix);
				full.push_back(':');
				full.append(key);
				dst[full] = value;
			}
		}
	}

	json::map movie_info::to_json() const {
		json::map result{};
		STORE(refs);
		STORE(genres);
		STORE(countries);
		STORE_OR_VALUE(age);
		STORE(tags);
		STORE(episodes);
		STORE(crew);
		STORE_PREFIXED(title);
		STORE_PREFIXED(tagline);
		STORE_PREFIXED(summary);
		STORE(image);
		STORE(dates);
		STORE(year);
		STORE(runtime);
		STORE(rating);
		STORE(video);
		return result;
	}

	json::conv_result load_tr_strings(
	    json::map const& data,
	    std::u8string_view prefix,
	    std::map<std::u8string, std::u8string>& dst,
	    std::string&) {
		for (auto const& [key, value] : data) {
			if (key == prefix) {
				auto str = cast<json::string>(value);
				if (!str) return json::conv_result::failed;
				dst[{}] = *str;
				continue;
			}
			if (key.starts_with(prefix) && key[prefix.length()] == u8':') {
				auto str = cast<json::string>(value);
				if (!str) return json::conv_result::failed;
				dst[key.substr(prefix.length() + 1)] = *str;
				continue;
			}
		}
		return json::conv_result::ok;
	}

	bool has_strings(json::map const& data) {
		for (auto const& [property, value] : data) {
			if (property == u8"names"sv) continue;
			auto items = cast<json::array>(value);
			if (!items) continue;
			for (auto const& item : *items) {
				if (cast<json::string>(item)) {
					return true;
				}
				auto arr = cast<json::array>(item);
				if (arr && !arr->empty() && cast<json::string>(arr->front()))
					return true;
			}
		}
		return false;
	}

	json::node const* at(json::map const& map, std::u8string const& key) {
		auto it = map.find(key);
		if (it == map.end()) return nullptr;
		return &it->second;
	}
	json::conv_result movie_info::load_crew(json::map const& data,
	                                        std::string& dbg) {
		auto result = json::conv_result::ok;
		auto outer_people_ptr = at(data, u8"people"s);
		auto crew_map = json::cast<json::map>(data, u8"crew"s);
		if (crew_map) {
			auto const outer_people_is_array =
			    !!json::cast<json::array>(outer_people_ptr);
			auto const inner_names_is_absent = !at(*crew_map, u8"names"s);
			auto const oldest_version =
			    !outer_people_is_array && inner_names_is_absent;

			if (oldest_version || has_strings(*crew_map)) {
				std::map<std::u8string, std::u8string> old_refs{};
				auto json_people = cast<json::map>(outer_people_ptr);
				LOAD_EX(map_from_json(json_people, old_refs, dbg));

				LOAD_EX(crew.rebuild(*crew_map, dbg, old_refs));

				dbg.append("\n- Old-style crew & cast was found"sv);
				return json::conv_result::updated;
			}

			if (outer_people_is_array) {
				LOAD_EX(::json::load(*json::cast<json::array>(outer_people_ptr),
				                     crew.names, dbg));
				LOAD_EX(crew.from_json_short(*crew_map, dbg));

				dbg.append("\n- People names outside crew object"sv);
				return json::conv_result::updated;
			}
		}

		LOAD(crew);
		return result;
	}

	json::conv_result movie_info::load_old_title(json::map const& data,
	                                             std::string& dbg) {
		old_title_info value{};
		auto const ret = json::load(data, u8"title"s, value, dbg);
		if (ret == ::json::conv_result::failed) return ret;
		if (!!value.local || !!value.orig) {
			// this is old-style JSON.
			if (!value.local && value.orig) value.local = *value.orig;
			auto& def = title.items[{}];
			def.text = *value.local;
			def.sort = value.sort;
			if (value.orig) {
				auto& def = title.items["<unk>"];
				def.text = *value.orig;
				def.sort = value.sort;
				def.original = true;
			}

			dbg.append("\n- Old-style title was found"sv);
			return ::json::conv_result::updated;
		}
		return ::json::conv_result::ok;
	}

	json::conv_result movie_info::from_json(json::map const& data,
	                                        std::string& dbg) {
		auto result = json::conv_result::ok;

		LOAD_EX(load_old_title(data, dbg));

		LOAD(refs);
		LOAD(genres);
		LOAD(countries);
		LOAD_OR_VALUE(age);
		LOAD(tags);
		LOAD(episodes);
		LOAD_EX(load_crew(data, dbg));
		LOAD_PREFIXED(title);
		LOAD_PREFIXED(tagline);
		LOAD_PREFIXED(summary);
		LOAD(image);
		LOAD(dates);
		LOAD(year);
		LOAD(runtime);
		LOAD(rating);
		LOAD(video);

		bool has_unk_original = false;
		bool has_other_original = false;

		for (auto const& [key, value] : title) {
			if (value.original) {
				if (key == "<?>"sv) {
					has_unk_original = true;
					if (has_other_original) break;
				} else {
					has_other_original = true;
					if (has_unk_original) break;
				}
			}
		}

		if (has_other_original && has_unk_original) {
			title.items.erase("<?>");
			result = json::conv_result::updated;
			dbg.append("\n- There is an unneeded title:<?>"sv);
		}

		return result;
	}

	MERGE_BEGIN(movie_info, prefer_title which_title)
	MERGE_ARRAY(refs);
	MERGE_ARRAY(genres);
	MERGE_ARRAY(countries);
	MERGE_ARRAY(age);
	MERGE_ARRAY(tags);
	MERGE_ARRAY(episodes);
	MERGE_OBJ(crew);
	MERGE_PREFIXED(title, which_title);
	MERGE_PREFIXED(tagline);
	MERGE_PREFIXED(summary);
	MERGE_OBJ(image);
	MERGE_OBJ(dates);
	MERGE_OPT(year);
	MERGE_OPT(runtime);
	MERGE_OPT(rating);
	MERGE_OBJ(video);
	MERGE_END()

	void movie_info::add_tag(std::u8string_view tag) {
		auto it = std::find(tags.begin(), tags.end(), tag);
		if (it == tags.end()) tags.emplace_back(tag.data(), tag.size());
	}

	void movie_info::remove_tag(std::u8string_view tag) {
		auto it = std::find(tags.begin(), tags.end(), tag);
		if (it != tags.end()) tags.erase(it);
	}

	bool movie_info::has_tag(std::u8string_view tag) const noexcept {
		auto it = std::find(tags.begin(), tags.end(), tag);
		return (it != tags.end());
	}

	std::u8string make_json(std::u8string_view file_root) {
		static constexpr auto ext = u8".json"sv;
		std::u8string result{};
		result.reserve(file_root.length() + ext.length());
		result.append(file_root);
		result.append(ext);
		return result;
	}

	bool movie_info::store(fs::path const& db_root,
	                       std::u8string_view dirname) const {
		auto const json_filename = db_root / "nfo"sv / make_json(dirname);

		std::error_code ec{};

		fs::create_directories(json_filename.parent_path(), ec);
		if (ec) return false;

		auto json = io::file::open(json_filename, "wb");
		if (!json) {
			std::cout << "Cannot open " << json_filename.string()
			          << " for writing\n";
			return false;
		}
		json::write_json(json.get(), to_json(), json::four_spaces);
		return true;
	}

	json::conv_result movie_info::load(fs::path const& db_root,
	                                   std::u8string_view dirname,
	                                   alpha_2_aliases const& aka,
	                                   std::string& dbg) {
		auto const json_filename = db_root / "nfo"sv / make_json(dirname);

		auto const data = io::contents(json_filename);
		auto node = json::read_json({data.data(), data.size()});

		auto result = json::conv_result::ok;
		LOAD_EX(::json::load(node, *this, dbg));

		if (map_countries(aka)) {
			result = json::conv_result::updated;
			dbg.append("\n- Country list updated to ISO alpha2"sv);
		}
		return result;
	}

	bool movie_info::map_countries(alpha_2_aliases const& aka) {
		std::vector<std::u8string> mapped_countries{};
		mapped_countries.reserve(countries.size());
		for (auto const& list : countries) {
			auto items = split_s('/', list);
			for (auto const& item : items)
				mapped_countries.emplace_back(aka.map(item));
		}

		if (mapped_countries != countries) {
			std::swap(mapped_countries, countries);
			return true;
		}
		return false;
	}

#ifdef MOVIES_HAS_NAVIGATOR
	bool movie_info::offline_images(fs::path const& db_root,
	                                nav::navigator& nav,
	                                uri const& referrer,
	                                std::u8string_view dirname) {
		auto mapping = rebase_all(image, dirname, uri::make_base(referrer));

		auto const img_dir = db_root / "img"sv;

		std::error_code ec{};
		fs::create_directories(img_dir / dirname, ec);
		if (ec) return false;

		std::set<std::chrono::sys_seconds> mtimes;

		for (auto const& [fname, url] : mapping) {
			auto filename = img_dir / fname;
			auto img = nav.open(prepareReq(url, referrer));
			if (!img.exists()) {
				std::cout << "Cannot download image for " << filename.string()
				          << ": " << img.status_text() << '\n';
				return false;
			}

			std::chrono::sys_seconds mtime{};
			if (auto last_modified =
			        img.headers().find_front(nav::header::Last_Modified)) {
				auto const mtime =
				    movies::dates_info::from_http_date(*last_modified);
				if (mtime && *mtime != std::chrono::sys_seconds{})
					mtimes.insert(*mtime);
			}

			auto file = io::file::open(filename, "wb");
			if (!file) {
				std::cout << "Cannot open " << filename.string()
				          << " for writing\n";
				return false;
			}

			std::string_view data{img.text()};
			auto const written =
			    std::fwrite(data.data(), 1, data.size(), file.get());
			if (written < data.size()) {
				std::cout << "Cannot write to " << filename.string() << '\n';
				return false;
			}
		}

		if (!mtimes.empty()) {
			auto const mtime =
			    std::reduce(std::execution::par_unseq, mtimes.begin(),
			                mtimes.end(), std::chrono::sys_seconds{},
			                [](auto const& lhs, auto const& rhs) {
				                return std::max(lhs, rhs);
			                });
			dates.poster = mtime;
		}

		return true;
	}
#endif
}  // namespace movies
