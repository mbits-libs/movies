// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <fmt/format.h>
#include <io/file.hpp>
#include <movies/db_info.hpp>
#include <movies/image_url.hpp>
#include <movies/movie_info.hpp>

#include "impl.hpp"

#if defined(MOVIES_HAS_NAVIGATOR)
#include <tangle/uri.hpp>
#include <utf/utf.hpp>
#endif

namespace movies::v1 {
	namespace {
		fs_string make_json(string_view_type file_root) {
			static constexpr auto ext = u8".json"sv;
			fs_string result{};
			result.reserve(file_root.length() + ext.length());
			result.append(as_fs_view(file_root));
			result.append(ext);
			return result;
		}

		template <typename String>
		struct helper {
			static inline String as_str(string_view_type str) {
				return String{str.data(), str.size()};
			}
		};

		template <>
		struct helper<string_view_type> {
			static inline string_view_type as_str(
			    string_view_type str) noexcept {
				return str;
			}
		};

		size_t calc_separators(char sep, string_view_type data, size_t max) {
			auto pos = data.find(sep);
			decltype(pos) prev = 0;

			size_t result = 1;

			while (max && pos != string_view_type::npos) {
				prev = pos + 1;
				pos = data.find(sep, prev);
				if (max != std::u8string::npos) --max;

				++result;
			}

			return result;
		}

		template <typename String>
		std::vector<String> split_impl(char sep,
		                               string_view_type data,
		                               size_t max) {
			std::vector<String> result{};

			result.reserve(calc_separators(sep, data, max));

			auto pos = data.find(sep);
			decltype(pos) prev = 0;

			while (max && pos != string_view_type::npos) {
				auto const view = data.substr(prev, pos - prev);
				prev = pos + 1;
				pos = data.find(sep, prev);
				if (max != std::u8string::npos) --max;

				result.push_back(helper<String>::as_str(view));
			}

			result.push_back(helper<String>::as_str(data.substr(prev)));

			return result;
		}

		std::vector<string_type> split_s(char sep,
		                                 string_view_type data,
		                                 size_t max = string_view_type::npos) {
			return split_impl<string_type>(sep, data, max);
		}

#if defined(MOVIES_HAS_NAVIGATOR)
		using namespace tangle;

		std::u8string lower(std::u8string_view str) {
			auto wide = utf::as_u32(str);
			for (auto& c : wide) {
				if (c < 256) c = std::tolower(c);
			}
			return utf::as_u8(wide);
		}

		std::u8string extension(std::u8string_view str) {
			auto src = uri{as_ascii_string_v(str)};

			auto ext = lower(
			    fs::path{as_fs_string_v(src.path())}.extension().u8string());
			if (ext.empty() || ext == u8".jpeg"sv) ext = u8".jpg"sv;
			return ext;
		}
#else
		std::u8string extension(std::u8string_view) { return u8".jpg"s; }
#endif

		void remap_img(std::optional<image_url>& dst,
		               string_view_type dirname,
		               std::string_view filename) {
			if (dst && (!dst->url || (dst->url && dst->url->empty())))
				dst = std::nullopt;
			if (!dst) return;

			string_type result{};
			auto ext = extension(*dst->url);
			result.reserve(dirname.length() + filename.length() + ext.length() +
			               1);
			result.append(dirname);
			result.push_back('/');
			result.append(as_view(filename));
			result.append(as_view(ext));
			dst->path = std::move(result);
		}

		void remap_img(translatable<image_url>& dst,
		               string_view_type dirname,
		               std::string_view filename) {
			for (auto& [lang, image] : dst) {
				auto subdir = as_string_v(dirname);
				if (!lang.empty()) {
					subdir.push_back('/');
					subdir.append(as_view(lang));
				}
				std::optional<image_url> sure{std::move(image)};
				remap_img(sure, subdir, filename);
				if (sure) image = std::move(*sure);
			}
		}

		void remap_all(movies::image_info& self, string_view_type dirname) {
			remap_img(self.highlight, dirname, "01-highlight"sv);
			for (auto& [lang, poster] : self.poster.items) {
				auto subdir = as_string_v(dirname);
				if (!lang.empty()) {
					subdir.push_back('/');
					subdir.append(as_view(lang));
				}
				remap_img(poster.small, subdir, "00-poster-0_small"sv);
				remap_img(poster.normal, subdir, "00-poster-1_normal"sv);
				remap_img(poster.large, subdir, "00-poster-2_large"sv);
			}

			size_t index{};
			for (auto& image : self.gallery) {
				std::optional<image_url> sure{std::move(image)};
				remap_img(sure, dirname,
				          fmt::format("02-gallery-{:02}", index++));
				if (sure) image = std::move(*sure);
			}
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

	void movie_info::add_tag(string_view_type tag) {
		auto it = std::find(tags.begin(), tags.end(), tag);
		if (it == tags.end()) tags.emplace_back(tag.data(), tag.size());
	}

	void movie_info::remove_tag(string_view_type tag) {
		auto it = std::find(tags.begin(), tags.end(), tag);
		if (it != tags.end()) tags.erase(it);
	}

	bool movie_info::has_tag(string_view_type tag) const noexcept {
		auto it = std::find(tags.begin(), tags.end(), tag);
		return (it != tags.end());
	}

	bool movie_info::store(fs::path const& db_root,
	                       string_view_type key) const {
		auto const json_filename = db_root / "nfo"sv / make_json(key);

		std::error_code ec{};

		fs::create_directories(json_filename.parent_path(), ec);
		if (ec) return false;

		auto json = io::file::open(json_filename, "wb");
		if (!json) {
			fmt::print("Cannot open {} for writing\n",
			           as_ascii_view(json_filename.generic_u8string()));
			return false;
		}
		json::write_json(json.get(), to_json(), json::four_spaces);
		return true;
	}

	json::conv_result movie_info::load(fs::path const& db_root,
	                                   string_view_type key,
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
		std::vector<string_type> mapped_countries{};
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

	void movie_info::map_images(string_view_type movie_id) {
		remap_all(image, movie_id);
	}

#if defined(MOVIES_HAS_NAVIGATOR)
	void movie_info::canonize_uris(tangle::uri const& base_url) {
		if (!base_url.empty()) {
			visit_image(image, [&](image_url& image) {
				if (!image.url) return;
				auto url = uri::canonical(
				    as_ascii_string(std::move(*image.url)), base_url);
				image.url = as_string_v(url.string());
			});
		}
	}
#endif
}  // namespace movies::v1
