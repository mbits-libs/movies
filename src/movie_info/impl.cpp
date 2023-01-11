// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <movies/movie_info.hpp>

#include "impl.hpp"
#include "person_info.hpp"

namespace movies::v1 {
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
			OP(v1::load(data, u8"text", text, dbg));
			OP(v1::load(data, u8"sort", sort, dbg));
			OP(v1::load(data, u8"original", original, dbg));
			if (!text) return json::conv_result::opt;
			this->text = *text;
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
			if (person.name == u8"N/A"sv) {
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
