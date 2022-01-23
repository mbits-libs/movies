// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <iostream>
#include <movies/db_info.hpp>
#include <movies/diff.hpp>
#include <movies/loader.hpp>

// #define DEBUG_LOADER
#ifdef DEBUG_LOADER
#include <format>
#include <io/file.hpp>
#include <movies/opt.hpp>
#endif

using namespace std::literals;

namespace movies {
	namespace {
#ifdef DEBUG_LOADER
		inline auto file_ref_mtime(movies::file_ref const& ref) {
			return ref.mtime;
		}
#endif

		fs::file_time_type get_mtime(fs::path const& path) {
			std::error_code ec{};
			auto const result = fs::last_write_time(path, ec);
			if (ec) return {};
			return result;
		}

		file_ref get_ref(fs::path const& path, string const& id) {
			return {id, get_mtime(path)};
		}

		file_ref info_ref(fs::path const& db_root, string const& id) {
			return get_ref(db_root / "nfo"sv / (id + u8".json"), id);
		}

		file_ref video_ref(fs::path const& videos_root, string const& id) {
			return get_ref(videos_root / (id + u8".mp4"), id);
		}

		map<string, movie_info> known_movies(fs::path const& db_root) {
			map<string, movie_info> result{};
			alpha_2_aliases aka{};
			aka.load(db_root);

			auto root = db_root / "nfo"sv;

			std::error_code ec{};
			fs::recursive_directory_iterator iterator{root, ec};
			if (ec) return result;

			for (auto&& entry : iterator) {
				if (entry.path().extension().generic_u8string() != u8".json")
					continue;
				auto json_path = entry.path();
				auto u8ident = fs::relative(json_path, root).generic_u8string();
				u8ident = u8ident.substr(0, u8ident.length() - 5);

				movie_info info{};
				auto const load_result = info.load(db_root, u8ident, aka);
				if (load_result == json::conv_result::failed) continue;
				if (load_result == json::conv_result::updated) {
					info.store(db_root, u8ident);
					fputc('.', stdout);
					fflush(stdout);
				}
				result.insert({u8ident, std::move(info)});
			}

			return result;
		}

		vector<string> downloaded_movies(fs::path const& videos_root) {
			vector<string> result{};

			std::error_code ec{};
			fs::recursive_directory_iterator iterator{videos_root, ec};
			if (ec) return result;

			for (auto&& entry : iterator) {
				if (entry.path().extension().generic_u8string() != u8".mp4")
					continue;
				auto movie_path = entry.path();
				auto u8ident =
				    fs::relative(movie_path, videos_root).generic_u8string();
				u8ident = u8ident.substr(0, u8ident.length() - 4);
				result.emplace_back(std::move(u8ident));
			}

			std::sort(result.begin(), result.end());

			return result;
		}

		vector<string> split_simple(vector<string>& infos,
		                            vector<string>& videos) {
			vector<string> result, infos_left, videos_left;

			auto it_infos = infos.begin(), it_videos = videos.begin();

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

		std::u8string make_title(std::u8string const& input) {
			auto const pos = std::u8string_view{input}.find_first_of(u8" \t"sv);
			if (pos != std::string_view::npos) return input;
			auto result = input;
			for (auto& byte : result) {
				if (byte == '-' || byte == '_') byte = ' ';
			}
			if (!result.empty())
				result.front() =
				    std::toupper(static_cast<unsigned char>(result.front()));
			return result;
		}

		movie_data make_empty(std::optional<file_ref> const& video_file,
		                      std::optional<file_ref> const& info_file) {
			movie_data result{{}, video_file, info_file};
			if (video_file)
				result.info.title.local = make_title(video_file->id);
			else if (info_file)
				result.info.title.local = make_title(info_file->id);

			return result;
		}

		template <typename Key,
		          typename Value,
		          typename Compare,
		          typename Allocator>
		auto keys_of(std::map<Key, Value, Compare, Allocator> const& map) {
			using ReboudAllocator =
			    std::allocator_traits<Allocator>::template rebind_alloc<Key>;
			std::vector<Key, ReboudAllocator> result{};
			result.reserve(map.size());
			std::transform(
			    map.begin(), map.end(), std::back_inserter(result),
			    [](auto const& pair) -> Key const& { return pair.first; });

			return result;
		}

#ifdef DEBUG_LOADER
		json::string to_string(fs::file_time_type mtime) {
			using namespace std::chrono;
			auto const sys = duration_cast<seconds>(
			    clock_cast<system_clock>(mtime).time_since_epoch());
			auto local =
			    zoned_time{current_zone(), sys_seconds{sys}}.get_local_time();
			auto result = std::format("{:%F %T}", local);
			return {reinterpret_cast<char8_t const*>(result.data()),
			        result.length()};
		}
#endif
	}  // namespace

	vector<movie_data> load_from(fs::path const& db_root,
	                             fs::path const& videos_root) {
		auto jsons = known_movies(db_root);
		auto infos = keys_of(jsons);
		auto videos = downloaded_movies(videos_root);

		auto both = split_simple(infos, videos);
		auto matching = differ{jsons, infos, videos}.calc();

		vector<movie_data> movies{};
		movies.reserve(both.size() + matching.size() + infos.size() +
		               videos.size());

		for (auto const& id : both) {
			auto it = jsons.find(id);
			if (it == jsons.end()) {
				[[unlikely]] movies.push_back(make_empty(
				    video_ref(videos_root, id), info_ref(db_root, id)));
				continue;
			}
			auto& mv = it->second;
			movies.push_back({std::move(mv), video_ref(videos_root, id),
			                  info_ref(db_root, id)});
		}

		for (auto const& diff : matching) {
			auto it = jsons.find(diff.info);
			if (it == jsons.end()) {
				[[unlikely]] movies.push_back(
				    make_empty(video_ref(videos_root, diff.video),
				               info_ref(db_root, diff.info)));
				continue;
			}
			auto& mv = it->second;
			movies.push_back({std::move(mv), video_ref(videos_root, diff.video),
			                  info_ref(db_root, diff.info)});
		}

		for (auto const& id : videos)
			movies.push_back(
			    make_empty(video_ref(videos_root, id), std::nullopt));

		for (auto const& id : infos) {
			auto it = jsons.find(id);
			if (it == jsons.end()) {
				[[unlikely]] movies.push_back(
				    make_empty(std::nullopt, info_ref(db_root, id)));
				continue;
			}
			auto& mv = it->second;
			movies.push_back(
			    {std::move(mv), std::nullopt, info_ref(db_root, id)});
		}

#ifdef DEBUG_LOADER
		{
			std::map<json::string, json::array> dbg{};
			for (auto const& data : movies) {
				auto const key =
				    (data.video_file || data.info_file) >> file_ref_mtime ||
				    fs::file_time_type{};
				auto const& title = data.info.title;
				auto value =
				    title.local || title.orig || title.sort || string{};
				dbg[to_string(key)].push_back(std::move(value));
			}
			json::map dbg_map{};
			for (auto& [key, titles] : dbg) {
				if (titles.size() == 1) {
					dbg_map[key] = std::move(titles[0]);
					continue;
				}
				dbg_map[key] = std::move(titles);
			}

			auto file = io::file::open("debug.json", "w");
			json::write_json(file.get(), dbg_map);
		}
#endif
		return movies;
	}
}  // namespace movies
