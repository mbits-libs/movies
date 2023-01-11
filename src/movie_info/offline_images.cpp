// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#if defined(MOVIES_HAS_NAVIGATOR)

#ifdef _MSC_VER
#include <format>
#else
#include <date/date.h>
#include <fmt/format.h>
#endif

#include <execution>
#include <io/file.hpp>
#include <movies/movie_info.hpp>
#include <set>

#ifdef _MSC_VER
namespace fmt {
	using std::format;
}
#endif

namespace movies::v1 {
	using namespace tangle;

	namespace {
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

		inline nav::request prepare_request(uri const& address,
		                                    uri const& referrer) {
			nav::request req{address};
			if (!referrer.empty()) req.referrer(referrer);
			return req;
		}

	}  // namespace

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
			auto img = nav.open(prepare_request(url, referrer));
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
}  // namespace movies::v1
#endif  // defined(MOVIES_HAS_NAVIGATOR)
