// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#ifdef _MSC_VER
#include <format>
#else
#include <date/date.h>
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

namespace movies {
	namespace {
		using namespace std::literals;
#ifdef MOVIES_HAS_NAVIGATOR
		using namespace tangle;
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

		json::conv_result array_from_json(json::array const* input,
		                                  std::vector<person_info>& output) {
			if (!input) return json::conv_result::ok;

			output.clear();
			output.reserve(input->size());

			auto result = json::conv_result::ok;
			for (auto const& node : *input) {
				output.emplace_back();
				person_info info{};
				auto const res = output.back().from_json(node);
				if (!is_ok(res)) result = res;
				if (result == json::conv_result::failed) break;
			}
			return result;
		}

		json::conv_result array_from_json(json::array const* input,
		                                  std::vector<std::u8string>& output) {
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
		    std::vector<std::u8string>& output) {
			auto const& str = std::get<0>(input);
			if (str) {
				output.clear();
				if (str->empty()) return json::conv_result::updated;
				output.push_back(*str);
				return json::conv_result::ok;
			}
			return array_from_json(std::get<1>(input), output);
		}

		json::conv_result map_from_json(json::map const* input,
		                                people_map& output) {
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

		json::conv_result merge_arrays(
		    std::vector<std::u8string>& old_data,
		    std::vector<std::u8string> const& new_data) {
			auto old_ = indexed(old_data);
			auto new_ = indexed(new_data, old_.size());
			auto index_old = size_t{0};
			auto index_new = size_t{0};
			auto const size_old = old_.size();
			auto const size_new = new_.size();

			using mid_pair = std::pair<std::u8string, size_t>;
			std::vector<mid_pair> midway{};
			while (index_old < size_old || index_new < size_new) {
				if (index_old == size_old) {
					midway.insert(midway.end(), new_.begin() + index_new,
					              new_.end());
					index_new = size_new;
					continue;
				}

				if (index_new == size_new) {
					midway.insert(midway.end(), old_.begin() + index_old,
					              old_.end());
					index_old = size_old;
					continue;
				}

				auto const& older = old_[index_old];
				auto const& newer = new_[index_new];

				if (older.first == newer.first) {
					midway.push_back(older);

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

			std::vector<std::u8string> final{};
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

#define TWC(fld) \
	if (auto cmp = lhs.fld <=> rhs.fld; cmp != 0) return cmp < 0

		struct person_info_t {
			std::u8string name;
			std::optional<std::u8string> key;
			std::optional<std::u8string> contribution;
			size_t original_position{};
			bool from_existing{};

			person_info_t merge(person_info_t const& new_data) && {
				// names must be equal
				key = new_data.key || key;
				contribution = new_data.contribution || contribution;
				return std::move(*this);
			}

			static std::vector<person_info_t> conv(
			    crew_info::people_list const& list,
			    people_map const& people,
			    bool from_existing) {
				std::vector<person_info_t> result{};
				result.resize(list.size());
				for (size_t index = 0; index < list.size(); ++index) {
					auto& dst = result[index];
					auto const& src = list[index];
					auto it = people.find(src.key);

					if (it == people.end()) {
						dst.name = src.key;
					} else {
						dst.name = it->second;
						dst.key = src.key;
					}

					dst.contribution = src.contribution;
					dst.original_position = index;
					dst.from_existing = from_existing;
				}

				std::stable_sort(
				    result.begin(), result.end(),
				    [](person_info_t const& lhs,
				       person_info_t const& rhs) -> bool {
					    TWC(name);
					    TWC(contribution);
					    TWC(key);
					    if (lhs.original_position != rhs.original_position)
						    return lhs.original_position <
						           rhs.original_position;
					    if (lhs.from_existing == rhs.from_existing)
						    return false;
					    return lhs.from_existing == false;
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
						              new_data.begin() + index_new,
						              new_data.end());
						index_new = size_new;
						continue;
					}

					if (index_new == size_new) {
						result.insert(result.end(),
						              old_data.begin() + index_old,
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
						    return lhs.from_existing == false;
					    if (lhs.original_position != rhs.original_position)
						    return lhs.original_position <
						           rhs.original_position;
					    TWC(name);
					    TWC(contribution);
					    TWC(key);
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
				           as_u8(std::format("02-gallery-{:02}"sv, index++)));
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

#define JSON_MAP_COPY(field_name)                                          \
	{                                                                      \
		if (auto const res = map_from_json(json_##field_name, field_name); \
		    res != json::conv_result::ok)                                  \
			result = res;                                                  \
		if (result == json::conv_result::failed) return result;            \
	}

#define MERGE(field_name)                      \
	auto const prev_##field_name = field_name; \
	field_name = new_data.field_name

#define CHANGED(field_name) (prev_##field_name != field_name)

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
		field_name = new_data.field_name || field_name;               \
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
#else
#define MERGE_OBJ(field_name, ...)                                            \
	{                                                                         \
		auto const sub_result =                                               \
		    field_name.merge(new_data.field_name __VA_OPT__(, ) __VA_ARGS__); \
		if (sub_result != json::conv_result::ok) result = sub_result;         \
		if (result == json::conv_result::failed) break;                       \
	}
#endif

#define MERGE_ARRAY(field_name)                                                \
	{                                                                          \
		auto const sub_result = merge_arrays(field_name, new_data.field_name); \
		if (sub_result != json::conv_result::ok) result = sub_result;          \
		if (result == json::conv_result::failed) break;                        \
	}

	json::node title_info::to_json() const {
		std::vector<std::pair<json::string, json::node>> values;
		values.reserve(3);
		if (local) values.push_back({u8"local", *local});
		if (orig) values.push_back({u8"orig", *orig});
		if (sort) values.push_back({u8"sort", *sort});

		if (values.empty()) return {};
		if (values.size() == 1) return std::move(values.front().second);
		return json::map{std::make_move_iterator(values.begin()),
		                 std::make_move_iterator(values.end())};
	}

	json::conv_result title_info::from_json(std::u8string const* title,
	                                        json::map const* map) {
		auto result = json::conv_result::ok;
		if (title) {
			local = *title;
			orig = sort = std::nullopt;
		} else if (map) {
			auto const& data = *map;
			LOAD(local);
			LOAD(orig);
			LOAD(sort);
		} else {
			fixup();
			return json::conv_result::opt;
		}

		return fixup() ? json::conv_result::updated : result;
	}

	json::conv_result title_info::merge(title_info const& new_data,
	                                    prefer_title which_title) {
		auto const prev_orig = orig;
		auto const prev_local = local;
		auto const prev_sort = sort;

		if (which_title == prefer_title::theirs) {
			local = new_data.local || local;
			orig = new_data.orig || new_data.local || orig || local;
			sort = new_data.sort || new_data.local || new_data.orig || sort ||
			       local || orig;
		} else {
			if (!orig) orig = new_data.orig || new_data.local;
			if (!sort) sort = new_data.sort || new_data.local || new_data.orig;
			if (!local) local = new_data.local;
		}

		fixup();

		return CHANGED(local) || CHANGED(orig) || CHANGED(sort)
		           ? json::conv_result::updated
		           : json::conv_result::ok;
	}

	bool title_info::fixup() {
		bool changed = false;

		if (local && local->empty()) {
			local = std::nullopt;
			changed = true;
		}
		if (orig && orig->empty()) {
			orig = std::nullopt;
			changed = true;
		}
		if (sort && sort->empty()) {
			sort = std::nullopt;
			changed = true;
		}

		if (local && orig && *local == *orig) {
			orig = std::nullopt;
			changed = true;
		}

		if (local && sort && *local == *sort) {
			sort = std::nullopt;
			changed = true;
		}

		if (!local && sort) {
			std::swap(local, sort);
			changed = true;
		}

		if (!local && orig) {
			std::swap(local, orig);
			changed = true;
		}

		return changed;
	}

	json::node person_info::to_json() const {
		if (!contribution) return key;
		return json::array{key, *contribution};
	}

	json::conv_result person_info::from_json(json::node const& data) {
		auto str = cast<std::u8string>(data);
		auto arr = cast<json::array>(data);

		if (str) {
			key = *str;
			contribution = std::nullopt;
			return json::conv_result::ok;
		}

		if (arr && arr->size() == 2) {
			auto crew_name = json::cast<std::u8string>(arr->front());
			auto contrib = json::cast<std::u8string>(arr->back());
			if (crew_name && contrib) {
				key = *crew_name;
				if (contrib->empty())
					contribution = std::nullopt;
				else
					contribution = *contrib;
				return json::conv_result::ok;
			}
		}

		return json::conv_result::failed;
	}

	json::node crew_info::to_json() const {
		json::map result{};
		STORE(directors);
		STORE(writers);
		STORE(cast);
		if (result.empty()) return {};
		return result;
	}

	json::conv_result crew_info::from_json(json::map const& data) {
		auto result = json::conv_result::ok;
		LOAD(directors);
		LOAD(writers);
		LOAD(cast);
		return result;
	}

	json::conv_result crew_info::merge(crew_info const& new_data,
	                                   people_map& people,
	                                   people_map const& new_people) {
		people_list crew_info::*lists[] = {
		    &crew_info::directors,
		    &crew_info::writers,
		    &crew_info::cast,
		};

		auto result = json::conv_result::ok;
		people_map used{};
		for (auto list : lists) {
			auto new_list = person_info_t::merge(
			    person_info_t::conv(this->*list, people, true),
			    person_info_t::conv(new_data.*list, new_people, false));

			people_list copied{};
			copied.reserve(new_list.size());
			for (auto& item : new_list) {
				person_info person{{}, std::move(item.contribution)};
				if (item.key) {
					person.key = *item.key;
					used[*item.key] = std::move(item.name);
				} else {
					person.key = std::move(item.name);
				}
				copied.emplace_back(std::move(person));
			}

			if (copied != this->*list) {
				std::swap(copied, this->*list);
				if (result == json::conv_result::ok)
					result = json::conv_result::updated;
			}
		}

		if (used != people) {
			std::swap(used, people);
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

	json::conv_result poster_info::from_json(json::map const& data) {
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

	json::conv_result image_info::from_json(json::map const& data) {
		auto result = json::conv_result::ok;
		LOAD_EX(json::load(data, NAMED(highlight)));
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

	json::conv_result dates_info::from_json(json::map const& data) {
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
		static constexpr char const* formats[] = {
		    "%a, %d %b %Y %T GMT",
		    "%A, %d-%b-%y %T GMT",
		};
		for (auto format : formats) {
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

	json::map movie_info::to_json() const {
		json::map result{};
		STORE(refs);
		STORE(title);
		STORE(genres);
		STORE(countries);
		STORE_OR_VALUE(age);
		STORE(tags);
		STORE(episodes);
		STORE(crew);
		STORE(people);
		STORE(summary);
		STORE(image);
		STORE(dates);
		STORE(year);
		STORE(runtime);
		STORE(rating);
		return result;
	}

	json::conv_result movie_info::from_json(json::map const& data) {
		auto result = json::conv_result::ok;
		JSON_CAST(people, json::map);

		LOAD(refs);
		LOAD_MULTI(title, std::u8string, json::map);
		LOAD(genres);
		LOAD(countries);
		LOAD_OR_VALUE(age);
		LOAD(tags);
		LOAD(episodes);
		LOAD(crew);
		JSON_MAP_COPY(people);
		LOAD(summary);
		LOAD(image);
		LOAD(dates);
		LOAD(year);
		LOAD(runtime);
		LOAD(rating);

		return result;
	}

	MERGE_BEGIN(movie_info, prefer_title which_title)
	MERGE_ARRAY(refs);
	MERGE_OBJ(title, which_title);
	MERGE_ARRAY(genres);
	MERGE_ARRAY(countries);
	MERGE_ARRAY(age);
	MERGE_ARRAY(tags);
	MERGE_ARRAY(episodes);
	MERGE_OBJ(crew, people, new_data.people);
	MERGE_OPT(summary);
	MERGE_OBJ(image);
	MERGE_OBJ(dates);
	MERGE_OPT(year);
	MERGE_OPT(runtime);
	MERGE_OPT(rating);
	MERGE_END()

	void movie_info::add_tag(std::u8string_view tag) {
		auto it = std::find(tags.begin(), tags.end(), tag);
		if (it == tags.end()) tags.push_back({tag.data(), tag.size()});
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
	                       std::u8string_view dirname) {
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
		json::write_json(json.get(), to_json());
		return true;
	}

	json::conv_result movie_info::load(fs::path const& db_root,
	                                   std::u8string_view dirname,
	                                   alpha_2_aliases const& aka) {
		auto const json_filename = db_root / "nfo"sv / make_json(dirname);

		auto const data = io::contents(json_filename);
		auto node = json::read_json({data.data(), data.size()});

		auto result = json::conv_result::ok;
		LOAD_EX(::json::load(node, *this));

		if (map_countries(aka)) result = json::conv_result::updated;
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
