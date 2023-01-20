// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#if defined(MOVIES_HAS_NAVIGATOR)

#include <date/date.h>
#include <fmt/format.h>

#include <execution>
#include <io/file.hpp>
#include <movies/movie_info.hpp>
#include <set>

namespace movies::v1 {
	using namespace tangle;

	namespace {
		inline nav::request prepare_request(uri const& address,
		                                    uri const& referrer) {
			nav::request req{address};
			if (!referrer.empty()) req.referrer(referrer);
			return req;
		}

		struct operation_src {
			image_op op;
			string_type src;
		};
		std::map<string_type, operation_src> reorganize(image_diff& diff) {
			std::map<string_type, operation_src> result{};
			for (auto& op : diff.ops) {
				auto it = result.lower_bound(op.dst);
				if (it == result.end() || it->first != op.dst) {
					result.insert(it,
					              {
					                  std::move(op.dst),
					                  {.op = op.op, .src = std::move(op.src)},
					              });
					continue;
				}
				if (op.op > it->second.op) {
					it->second = {.op = op.op, .src = std::move(op.src)};
				}
			}
			return result;
		}

		class fs_ops {
		public:
			bool store(fs::path const& dst,
			           std::span<char const> bytes,
			           bool debug_on) {
				std::error_code ignore{};
				fs::create_directories(dst.parent_path(), ignore);
				if (ignore) {
					fmt::print(stderr, "Cannot create directory for {}: {}\n",
					           as_ascii_view(dst.generic_u8string()),
					           ignore.message());

					return false;
				}

				auto file = io::file::open(dst, "wb");
				if (!file) {
					fmt::print(stderr, "Cannot open {} for writing\n",
					           as_ascii_view(dst.generic_u8string()));
					return false;
				}

				auto const written =
				    std::fwrite(bytes.data(), 1, bytes.size(), file.get());
				if (debug_on) {
					fmt::print(stderr, "-- writen {} for {}\n", written,
					           as_ascii_view(dst.generic_u8string()));
				}

				if (written < bytes.size()) {
					fmt::print(stderr, "Cannot write to {}\n",
					           as_ascii_view(dst.generic_u8string()));
					return false;
				}

				return true;
			}

			bool remove(fs::path const& dst, bool debug_on) {
				if (debug_on) {
					fmt::print(
					    stderr, "-- remove {}\n",
					    as_ascii_view(dst.parent_path().generic_u8string()));
				}
				std::error_code ignore{};
				fs::remove(dst, ignore);
				if (ignore) {
					fmt::print(stderr, "Cannot remove {}: {}\n",
					           as_ascii_view(dst.generic_u8string()),
					           ignore.message());

					return false;
				}
				dirs_.insert(dst.parent_path());

				return true;
			}

			bool rename(fs::path const& src,
			            fs::path const& dst,
			            bool debug_on) {
				if (debug_on) {
					fmt::print(
					    stderr, "-- move {} -> {}\n",
					    as_ascii_view(src.parent_path().generic_u8string()),
					    as_ascii_view(dst.parent_path().generic_u8string()));
				}

				std::error_code ignore{};
				fs::create_directories(dst.parent_path(), ignore);
				if (ignore) {
					fmt::print(stderr, "Cannot create directory for {}: {}\n",
					           as_ascii_view(dst.generic_u8string()),
					           ignore.message());

					return false;
				}
				fs::rename(src, dst, ignore);
				if (ignore) {
					fmt::print(stderr, "Cannot move {} to {}: {}\n",
					           as_ascii_view(src.generic_u8string()),
					           as_ascii_view(dst.generic_u8string()),
					           ignore.message());
					return false;
				}

				dirs_.insert(src.parent_path());
				return true;
			}

			void cleanup() {
				std::vector<fs::path> stack{
				    std::make_move_iterator(dirs_.begin()),
				    std::make_move_iterator(dirs_.end())};

				while (!stack.empty()) {
					auto dir = std::move(stack.back());
					stack.pop_back();
					std::error_code ignore{};
					if (!fs::is_empty(dir, ignore)) continue;
					fs::remove(dir, ignore);
					if (ignore) continue;
					auto parent = dir.parent_path();
					if (parent != dir) stack.push_back(parent);
				}
			}

		private:
			std::set<fs::path> dirs_{};
		};
	}  // namespace

	bool movie_info::download_images(std::filesystem::path const& db_root,
	                                 tangle::nav::navigator& nav,
	                                 image_diff& diff,
	                                 string_view_type movie_id,
	                                 tangle::uri const& referer,
	                                 bool debug) {
		std::error_code ec{};
		auto const img_root = db_root / "img"sv;
		fs::create_directories(img_root / movie_id, ec);
		if (ec) return false;

		auto mapping = reorganize(diff);
		fs_ops ops{};
		std::set<std::chrono::sys_seconds> mtimes{};
		for (auto const& [dst, action] : mapping) {
			switch (action.op) {
				case image_op::rm:
					if (dst.empty()) break;
					if (!ops.remove(img_root / as_fs_view(dst), debug))
						return false;
					break;
				case image_op::move:
					if (dst.empty() || action.src.empty()) break;
					if (!ops.rename(img_root / as_fs_view(action.src),
					                img_root / as_fs_view(dst), debug))
						return false;
					break;
				case image_op::download: {
					if (dst.empty() || action.src.empty()) break;
					auto const url = as_ascii_view(action.src);
					if (debug)
						fmt::print(stderr, "-- download {} from {}\n",
						           as_ascii_view(dst), url);
					auto img = nav.open(prepare_request(url, referer));
					if (!img.exists()) {
						fmt::print(stderr,
						           "Cannot download image from {}: {}\n", url,
						           img.status_text());
						continue;
					}

					if (!ops.store(img_root / as_fs_view(dst), img.text(),
					               debug))
						return false;

					std::chrono::sys_seconds mtime{};
					if (auto last_modified = img.headers().find_front(
					        nav::header::Last_Modified)) {
						auto const mtime =
						    movies::dates_info::from_http_date(*last_modified);
						if (mtime && *mtime != std::chrono::sys_seconds{})
							mtimes.insert(*mtime);
					}
					break;
				}
			}
		}

		ops.cleanup();

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
