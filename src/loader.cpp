// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <io/file.hpp>
#include <iostream>
#include <movies/db_info.hpp>
#include <movies/diff.hpp>
#include "movie_info/impl.hpp"

using namespace std::literals;

namespace movies {
	namespace {
		template <typename TimePoint>
		auto pre_20_file_time_type(TimePoint const& fs) {
			using namespace std::chrono;
			if constexpr (std::same_as<TimePoint, system_clock::time_point>) {
				return date::floor<seconds>(fs);
			} else {
				auto wall = time_point_cast<system_clock::duration>(
				    fs - TimePoint::clock::now() + system_clock::now());
				return date::floor<seconds>(wall);
			}
		}

		inline auto fs2wall(std::filesystem::file_time_type const& fs) {
#if __cpp_lib_chrono >= 201907L
			using namespace std::chrono;
			auto const wall = clock_cast<system_clock>(fs);
			return floor<seconds>(wall);
#else
			return pre_20_file_time_type(fs);
#endif
		}

		date::sys_seconds get_mtime(fs::path const& path) {
			std::error_code ec{};
			auto const result = fs::last_write_time(path, ec);
			if (ec) return {};
			return fs2wall(result);
		}

		file_ref get_ref(fs::path const& path, fs_string const& id) {
			return {as_string_v(id), get_mtime(path)};
		}

		file_ref info_ref(movies_dirs const& dirs, fs_string const& id) {
			return get_ref(dirs.infos / (id + u8".json"), id);
		}

		file_ref video_ref(movies_dirs const& dirs, fs_string const& id) {
			auto mp4 = dirs.videos / (id + u8".mp4");
			if (std::filesystem::exists(mp4)) return get_ref(mp4, id);
			auto mkv = dirs.videos / (id + u8".mkv");
			if (std::filesystem::exists(mkv)) return get_ref(mkv, id);
			return get_ref(mp4, id);
		}

		map<fs_string, movie_info> known_movies(movies_dirs const& dirs,
		                                        bool store_updates) {
			map<fs_string, movie_info> result{};
			alpha_2_aliases aka{};
			aka.load(dirs.infos / ".."sv);

			std::error_code ec{};
			fs::recursive_directory_iterator iterator{dirs.infos, ec};
			if (ec) return result;

			static constexpr auto ext = u8".json"sv;
			for (auto&& entry : iterator) {
				if (fs::is_directory(entry.status())) continue;
				if (entry.path().extension().u8string() != ext) continue;
				auto const& json_path = entry.path();
				auto u8ident =
				    fs::relative(json_path, dirs.infos).generic_u8string();
				u8ident = u8ident.substr(0, u8ident.length() - ext.length());

				movie_info info{};
				std::string debug{};
				auto const load_result =
				    info.load(dirs.infos, as_view(u8ident), aka, debug);
				if (load_result == json::conv_result::failed) continue;
				if (load_result == json::conv_result::updated) {
					if (store_updates) {
						info.store(dirs.infos, as_view(u8ident));
						fputc('.', stdout);
						fflush(stdout);
					} else {
						fprintf(stdout, "%.*s:%s\n",
						        static_cast<int>(u8ident.size()),
						        reinterpret_cast<char const*>(u8ident.data()),
						        debug.c_str() ? debug.c_str() : "<null>");
					}
				}
				result.insert({u8ident, std::move(info)});
			}

			return result;
		}

		vector<fs_string> downloaded_movies(movies_dirs const& dirs) {
			vector<fs_string> result{};

			std::error_code ec{};
			fs::recursive_directory_iterator iterator{dirs.videos, ec};
			if (ec) return result;

			for (auto&& entry : iterator) {
				if (fs::is_directory(entry.status())) continue;
				auto const ext = entry.path().extension().u8string();
				if (ext != u8".mp4"sv && ext != u8".mkv") continue;
				auto const& movie_path = entry.path();
				auto u8ident =
				    fs::relative(movie_path, dirs.videos).generic_u8string();
				u8ident = u8ident.substr(0, u8ident.length() - ext.length());
				result.emplace_back(std::move(u8ident));
			}

			std::sort(result.begin(), result.end());

			return result;
		}

		vector<fs_string> split_simple(vector<fs_string>& infos,
		                               vector<fs_string>& videos) {
			vector<fs_string> result;
			vector<fs_string> infos_left;
			vector<fs_string> videos_left;

			auto it_infos = infos.begin();
			auto it_videos = videos.begin();

			while (it_infos != infos.end() || it_videos != videos.end()) {
				if (it_infos == infos.end()) {
					videos_left.insert(videos_left.end(),
					                   std::make_move_iterator(it_videos),
					                   std::make_move_iterator(videos.end()));
					break;
				}

				if (it_videos == videos.end()) {
					infos_left.insert(infos_left.end(),
					                  std::make_move_iterator(it_infos),
					                  std::make_move_iterator(infos.end()));
					break;
				}

				if (*it_infos == *it_videos) {
					result.push_back(std::move(*it_infos));
					++it_infos;
					++it_videos;
					continue;
				}

				if (*it_infos < *it_videos) {
					infos_left.push_back(std::move(*it_infos));
					++it_infos;
					continue;
				}

				videos_left.push_back(std::move(*it_videos));
				++it_videos;
			}

			std::swap(infos, infos_left);
			std::swap(videos, videos_left);
			return result;
		}

		string_type make_title(std::u8string_view input) {
			auto const pos = input.find_first_of(u8" \t"sv);
			if (pos != std::string_view::npos) return as_string_v(input);
			auto result = as_string_v(input);
			for (auto& byte : result) {
				if (byte == '-' || byte == '_' || byte == '.' || byte == '/')
					byte = ' ';
			}
			if (!result.empty())
				result.front() =
				    std::toupper(static_cast<unsigned char>(result.front()));
			return result;
		}

		loaded_movie make_empty(std::optional<file_ref> const& video_file,
		                        std::optional<file_ref> const& info_file) {
			loaded_movie result{{}, video_file, info_file};
			if (video_file)
				result.title.items[{}].text = make_title(video_file->id);
			else if (info_file)
				result.title.items[{}].text = make_title(info_file->id);

			return result;
		}

		template <typename Key,
		          typename Value,
		          typename Compare,
		          typename Allocator>
		auto keys_of(std::map<Key, Value, Compare, Allocator> const& map) {
			using ReboudAllocator = typename std::allocator_traits<
			    Allocator>::template rebind_alloc<Key>;
			std::vector<Key, ReboudAllocator> result{};
			result.reserve(map.size());
			std::transform(
			    map.begin(), map.end(), std::back_inserter(result),
			    [](auto const& pair) -> Key const& { return pair.first; });

			return result;
		}

		struct db_type_traits
		    : public enum_traits_helper<db_type_traits, db_type> {
			using helper = enum_traits_helper<db_type_traits, db_type>;
			using name_type = helper::name_type;
			static std::span<name_type const> names() noexcept {
				static constexpr name_type enum_names[] = {
#define X_NAME(NAME) {u8## #NAME##sv, db_type::NAME},
				    DB_TYPE_X(X_NAME)
#undef X_NAME
				};
				return {std::data(enum_names), std::size(enum_names)};
			}
		};

		static constexpr auto DIR_DB = "db"sv;
		static constexpr auto DIR_NFO = "nfo"sv;
		static constexpr auto DIR_IMG = "img"sv;
		static constexpr auto DIR_VIDEOS = "videos"sv;
	}  // namespace

	void movies_config::read(fs::path const& config_filename,
	                         callback const& cb) {
		auto const data = io::contents(config_filename);
		auto node = json::read_json({data.data(), data.size()});
		auto json_path_str = cast<json::string>(node, u8"path"s);
		auto json_path_dict = cast<json::map>(node, u8"path"s);

		auto json_title = cast<json::string>(node, u8"title"s);
		auto json_adult = cast<json::string>(node, u8"adult"s);

		fs::path cfg_dir = config_filename.parent_path();

		*this = movies_config{};
		if (json_title) title = as_string_v(*json_title);
		if (json_adult) adult = db_type_traits::value_for(*json_adult);

#define DIR_NFO__ cfg_dir / DIR_DB / DIR_NFO
#define DIR_IMG__ cfg_dir / DIR_DB / DIR_IMG
#define DIR_VIDEOS__ cfg_dir / DIR_VIDEOS

		if (json_path_str) {
			cfg_dir /= as_fs_view(*json_path_str);
			dirs.infos = DIR_NFO__;
			dirs.images = DIR_IMG__;
			dirs.videos = DIR_VIDEOS__;
		} else if (json_path_dict) {
			auto json_path_db = cast<json::string>(node, u8"db"s);
			auto json_path_nfo = cast<json::string>(node, u8"nfo"s);
			auto json_path_img = cast<json::string>(node, u8"img"s);
			auto json_path_video = cast<json::string>(node, u8"video"s);

			dirs.infos = json_path_nfo ? cfg_dir / as_fs_view(*json_path_nfo)
			             : json_path_db
			                 ? cfg_dir / as_fs_view(*json_path_db) / DIR_NFO
			                 : DIR_NFO__;

			dirs.images = json_path_img ? cfg_dir / as_fs_view(*json_path_img)
			              : json_path_db
			                  ? cfg_dir / as_fs_view(*json_path_db) / DIR_IMG
			                  : DIR_IMG__;
			dirs.videos = json_path_video
			                  ? cfg_dir / as_fs_view(*json_path_video)
			                  : DIR_VIDEOS__;
		} else {
			dirs.infos = DIR_NFO__;
			dirs.images = DIR_IMG__;
			dirs.videos = DIR_VIDEOS__;
		}

#undef DIR_NFO__
#undef DIR_IMG__
#undef DIR_VIDEOS__

		if (cb) cb(*this, cfg_dir, node);
	}

	movies_config movies_config::from_dirs(
	    std::optional<fs::path> const& db_dir,
	    std::optional<fs::path> const& videos_dir) {
		auto db = db_dir ? *db_dir : fs::path{DIR_DB};
		movies_config result{
		    .dirs = {
		        .infos = db / DIR_NFO,
		        .images = db / DIR_IMG,
		        .videos = videos_dir ? *videos_dir : fs::path{"videos"sv},

		    }};
		return result;
	}

	vector<loaded_movie> movies_config::load(bool store_updates) const {
		auto jsons = known_movies(dirs, store_updates);
		auto infos = keys_of(jsons);
		auto videos = downloaded_movies(dirs);

		auto both = split_simple(infos, videos);
		auto matching = differ{jsons, infos, videos}.calc();

		vector<loaded_movie> movies{};
		movies.reserve(both.size() + matching.size() + infos.size() +
		               videos.size());

		for (auto const& id : both) {
			auto it = jsons.find(id);
			if (it == jsons.end()) {
				[[unlikely]] movies.push_back(
				    make_empty(video_ref(dirs, id), info_ref(dirs, id)));
				continue;
			}
			auto& mv = it->second;
			movies.push_back(
			    {std::move(mv), video_ref(dirs, id), info_ref(dirs, id)});
		}

		for (auto const& diff : matching) {
			auto it = jsons.find(diff.info);
			if (it == jsons.end()) {
				[[unlikely]] movies.push_back(make_empty(
				    video_ref(dirs, diff.video), info_ref(dirs, diff.info)));
				continue;
			}
			auto& mv = it->second;
			movies.push_back({std::move(mv), video_ref(dirs, diff.video),
			                  info_ref(dirs, diff.info)});
		}

		for (auto const& id : videos)
			movies.push_back(make_empty(video_ref(dirs, id), std::nullopt));

		for (auto const& id : infos) {
			auto it = jsons.find(id);
			if (it == jsons.end()) {
				[[unlikely]] movies.push_back(
				    make_empty(std::nullopt, info_ref(dirs, id)));
				continue;
			}
			auto& mv = it->second;
			movies.push_back({std::move(mv), std::nullopt, info_ref(dirs, id)});
		}

		return movies;
	}
}  // namespace movies
