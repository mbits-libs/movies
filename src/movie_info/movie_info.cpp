// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <io/file.hpp>
#include <movies/db_info.hpp>
#include <movies/movie_info.hpp>

#include "impl.hpp"

namespace movies::v1 {
	namespace {
		std::u8string make_json(std::u8string_view file_root) {
			static constexpr auto ext = u8".json"sv;
			std::u8string result{};
			result.reserve(file_root.length() + ext.length());
			result.append(file_root);
			result.append(ext);
			return result;
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
	}  // namespace

	json::conv_result movie_info::load_postproc(std::string& dbg) {
		auto result = json::conv_result::ok;
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

		if (version == 0) {
			version = 1;
			result = json::conv_result::updated;
			dbg.append("\n- Version is missing"sv);
		}

		return result;
	}

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

	bool movie_info::store(fs::path const& db_root,
	                       std::u8string_view key) const {
		auto const json_filename = db_root / "nfo"sv / make_json(key);

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
	                                   std::u8string_view key,
	                                   alpha_2_aliases const& aka,
	                                   std::string& dbg) {
		auto const json_filename = db_root / "nfo"sv / make_json(key);

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
}  // namespace movies::v1
