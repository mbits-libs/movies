// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include "impl.hpp"
#include <fmt/format.h>
#include <movies/image_url.hpp>
#include <set>
#include "person_info.hpp"

#if defined(MOVIES_HAS_NAVIGATOR)
#include <tangle/uri.hpp>
#endif

namespace movies::v1 {
	static_assert(MineOrTheirs<prefer_title>);

	json::node title_info::to_json() const {
		std::vector<std::pair<json::string, json::node>> values;
		values.reserve(3);
		values.emplace_back(u8"text", as_json_string_v(text));
		if (sort) values.emplace_back(u8"sort", as_json_string_v(*sort));
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
			text = as_string_v(*json_text);
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
			OP(v1::load(data, u8"text", text, dbg));
			OP(v1::load(data, u8"sort", sort, dbg));
			OP(v1::load(data, u8"original", original, dbg));
			if (!text) return json::conv_result::opt;
			this->text = as_string(std::move(*text));
		} else {
			return json::conv_result::opt;
		}
		OP(load_postproc(dbg));

		return result;
	}

	json::conv_result title_info::merge(title_info const& new_data,
	                                    prefer_title which_title) {
#define CHANGED(X) (prev_##X != X)
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

		merge_postproc();

		return CHANGED(text) || CHANGED(sort) || CHANGED(original)
		           ? json::conv_result::updated
		           : json::conv_result::ok;
	}

	json::conv_result title_info::load_postproc(std::string& dbg) {
		bool changed = false;

		if (text.empty() && sort) text = *sort;
		if (sort && text == *sort) sort = std::nullopt;

		if (sort && sort->empty()) {
			sort = std::nullopt;
			dbg.append("\n- Empty sort title string found"sv);
			changed = true;
		}

		return changed ? json::conv_result::updated : json::conv_result::ok;
	}

	json::conv_result title_info::merge_postproc() {
		std::string ignore{};
		return load_postproc(ignore);
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

			std::vector<role_info> copied{};
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

	json::conv_result crew_info::load_postproc(std::string& dbg) {
		long long id{};
		for (auto const& person : names) {
			if (person.name == as_view("N/A"sv)) {
				break;
			}
			++id;
		}

		if (static_cast<size_t>(id) < names.size()) {
			std::vector<role_info>* lists[] = {&directors, &writers, &cast};

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

	json::node role_info::to_json() const {
		if (!contribution) return id;
		return json::array{id, as_json_string_v(*contribution)};
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
					contribution = as_string_v(*contrib);
				return json::conv_result::ok;
			}
		}

		return json::conv_result::failed;
	}

	json::node image_url::to_json() const {
		if (!url) return as_json_string_v(path);
		if (path.empty()) return as_json_string_v(*url);
		return json::array{as_json_string_v(path), as_json_string_v(*url)};
	}

	json::conv_result image_url::from_json(json::node const& data,
	                                       std::string& dbg) {
		auto ref = cast<json::string>(data);
		auto arr = cast<json::array>(data);

		if (ref) {
			path = as_string_v(*ref);
			url = std::nullopt;
#if defined(MOVIES_HAS_NAVIGATOR)
			tangle::uri uri{as_ascii_view(path)};
			if (uri.has_scheme() && uri.has_authority()) {
				url = std::move(path);
				path.clear();
			}
#endif
			return json::conv_result::ok;
		}

		if (arr && arr->size() == 2) {
			auto ref = json::cast<json::string>(arr->front());
			auto address = json::cast<json::string>(arr->back());
			if (ref && address) {
				path = as_string_v(*ref);
				if (address->empty())
					url = std::nullopt;
				else
					url = as_string_v(*address);
				return json::conv_result::ok;
			}
		}

		return json::conv_result::failed;
	}

	json::conv_result image_url::merge(image_url const& new_data) {
		auto result = json::conv_result::ok;
		auto const prev = path;
		if (!url || url->empty()) {
			if (new_data.url && !new_data.url->empty())
				result = json::conv_result::updated;
			path = new_data.path;
			url = new_data.url;
		}
		return result;
	}

#define OPT(CURR, NEW)                                       \
	OP(CURR&& NEW ? CURR->merge(*NEW)                        \
	   : NEW      ? (CURR = NEW, json::conv_result::updated) \
	              : json::conv_result::ok)

	json::conv_result poster_info::merge(poster_info const& new_data) {
		auto result = json::conv_result::ok;
		OPT(small, new_data.small);
		OPT(large, new_data.large);
		OPT(normal, new_data.normal);
		return result;
	}

	bool empty(image_url const& data) noexcept {
		return data.path.empty() && (!data.url || data.url->empty());
	}

	bool empty(std::optional<image_url> const& data) noexcept {
		return !data || empty(*data);
	}

	bool empty(poster_info const& data) noexcept {
		return empty(data.small) && empty(data.normal) && empty(data.large);
	}

	json::conv_result clear(poster_info& data) noexcept {
		auto changed = json::conv_result::ok;
		if (empty(data.small)) {
			data.small = std::nullopt;
			changed = json::conv_result::updated;
		}
		if (empty(data.normal)) {
			data.normal = std::nullopt;
			changed = json::conv_result::updated;
		}
		if (empty(data.large)) {
			data.large = std::nullopt;
			changed = json::conv_result::updated;
		}
		return changed;
	}

	template <typename T>
	json::conv_result clear(translatable<T>& data) noexcept {
		std::vector<std::string> keys{};
		keys.reserve(data.items.size());
		for (auto const& [key, value] : data.items) {
			if (empty(value)) keys.push_back(key);
		}

		auto changed = json::conv_result::ok;
		for (auto const& key : keys) {
			data.items.erase(key);
			changed = json::conv_result::updated;
		}
		return changed;
	}

	string_view_type common_prefix(string_view_type prev,
	                               bool& initialized,
	                               string_view_type url) {
		if (!initialized) {
			initialized = !url.empty();
			return url;
		}
		for (size_t index = 0; index < prev.length(); ++index) {
			if (index == url.length() || prev[index] != url[index])
				return prev.substr(0, index);
		}
		return prev;
	}

	string_view_type common_prefix(string_view_type prev,
	                               bool& initialized,
	                               std::optional<image_url> const& url) {
		if (!url) return prev;
		return common_prefix(prev, initialized, url->path);
	}

	string_view_type common_prefix(string_view_type prev,
	                               bool& initialized,
	                               poster_info const& url) {
		prev = common_prefix(prev, initialized, url.small);
		prev = common_prefix(prev, initialized, url.normal);
		prev = common_prefix(prev, initialized, url.large);
		return prev;
	}

	template <typename T>
	string_view_type common_prefix(string_view_type prev,
	                               bool& initialized,
	                               translatable<T> const& url) {
		for (auto const& [_, item] : url.items) {
			prev = common_prefix(prev, initialized, item);
		}

		return prev;
	}

	json::conv_result image_info::merge(image_info const& new_data,
	                                    image_diff* image_changes) {
		auto const copy = *this;

		auto result = json::conv_result::ok;
		OP(v1::merge(highlight, new_data.highlight));
		OP(v1::merge(poster, new_data.poster));

		std::vector<string_view_type> curr, next;
		std::map<string_view_type, string_view_type> curr_rev, next_rev;

		curr.reserve(gallery.size());
		for (auto const& [lang, hl] : new_data.highlight.items) {
			if (!hl.url || hl.url->empty()) continue;
			auto it = highlight.items.find(lang);
			if (it != highlight.end() && hl.url == it->second.url) continue;

			bool found = false;
			for (auto const& image : gallery) {
				if (image.url == hl.url) {
					found = true;
					break;
				}
			}
			if (found) continue;

			for (auto const& image : curr) {
				if (image == hl.url) {
					found = true;
					break;
				}
			}
			if (found) continue;

			curr.push_back(*hl.url);
			curr_rev[*hl.url] = hl.path;
		}
		std::set<string_type> old_gallery_files{};
		for (auto const& image : gallery) {
			if (!image.path.empty()) old_gallery_files.insert(image.path);

			if (!image.url || image.url->empty()) {
				result = json::conv_result::updated;
				continue;
			}

			curr.push_back(*image.url);
			curr_rev[*image.url] = image.path;
		}

		next.reserve(gallery.size());
		for (auto const& image : new_data.gallery) {
			if (!image.url || image.url->empty()) continue;

			next.push_back(*image.url);
			next_rev[*image.url] = image.path;
		}

		OP(v1::merge(curr, next));

		OP(clear(highlight));
		OP(clear(poster));

		auto prefix = as_string_v([&] {
			bool initied{false};
			auto movie_id = common_prefix({}, initied, highlight);
			movie_id = common_prefix(movie_id, initied, poster);
			for (auto const& image : gallery) {
				movie_id = common_prefix(movie_id, initied, image);
			}
			auto pos = movie_id.rfind('/');
			if (pos != std::string::npos) movie_id = movie_id.substr(0, pos);
			if (!movie_id.empty() && movie_id.back() == '/')
				movie_id = movie_id.substr(0, movie_id.length() - 1);
			return movie_id;
		}());
		if (!prefix.empty()) prefix.push_back('/');

		std::vector<image_url> new_gallery{};
		new_gallery.reserve(curr.size());
		size_t index{};
		for (auto const& url : curr) {
			auto path = [&]() -> string_view_type {
				auto it = curr_rev.find(url);
				if (it != curr_rev.end()) return it->second;
				it = next_rev.find(url);
				if (it != next_rev.end()) return it->second;
				return {};
			}();
			auto filename = as_string(fmt::format(
			    "{}02-gallery-{:02}{}", as_ascii_view(prefix), index,
			    path.empty()
			        ? ".jpg"sv
			        : as_ascii_view(fs::path{path}.extension().u8string())));
			++index;
			old_gallery_files.erase(filename);

			new_gallery.push_back({
			    .path = std::move(filename),
			    .url = as_string_v(url),
			});
		}
		std::swap(gallery, new_gallery);

		if (image_changes) {
			for (auto& old_file : old_gallery_files) {
				image_changes->ops.push_back(
				    {.op = image_op::rm, .dst = std::move(old_file)});
			}

			visit_image(*this, [&](image_url const& image) {
				if (!image.url || image.url->empty() || image.path.empty())
					return;
				image_changes->ops.push_back({.op = image_op::download,
				                              .src = *image.url,
				                              .dst = image.path});
			});
		}

		return result;
	}

	json::node person_name::to_json() const {
		if (refs.empty()) return as_json_string_v(name);
		json::array result{};
		result.reserve(refs.size() + 1);
		result.push_back(as_json_string_v(name));
		std::transform(refs.begin(), refs.end(), std::back_inserter(result),
		               [](auto const& item) { return as_json_string_v(item); });
		return json::node{std::move(result)};
	}

	json::conv_result person_name::from_json(json::node const& data,
	                                         std::string&) {
		auto str = cast<std::u8string>(data);
		auto arr = cast<json::array>(data);

		if (str) {
			name = as_string_v(*str);
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
					name = as_string_v(*val);
					continue;
				}
				refs.push_back(as_string_v(*val));
			}
			if (name_set) return json::conv_result::ok;
		}

		return json::conv_result::failed;
	}

	std::optional<date::sys_seconds> dates_info::from_http_date(
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

	bool video_marker::equiv(video_marker const& rhs) const noexcept {
		// update comment on merge...
		return type == rhs.type && start == rhs.start && stop == rhs.stop;
	}
}  // namespace movies::v1
