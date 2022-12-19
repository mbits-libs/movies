// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#include <movies/db_info.hpp>
#include <movies/diff.hpp>
#include <movies/movie_info.hpp>
#include "difflib.hpp"

namespace movies {
	namespace {
		inline double similar(std::u8string_view a, std::u8string_view b) {
			return difflib::MakeSequenceMatcher(a, b).ratio();
		}

		vector<diff> diffs(map<string, movie_info> const& jsons,
		                   vector<string>& infos,
		                   vector<string>& videos) {
			vector<diff> close_calls{};
			close_calls.reserve(infos.size() * videos.size());

			alpha_2_aliases empty{};

			for (auto const& info : infos) {
				auto it = jsons.find(info);
				if (it == jsons.end()) [[unlikely]]
					continue;
				auto const& mv = it->second;

				auto info_key = info;
				for (auto& c : info_key) {
					if (c == '-' || c == '_') c = ' ';
				}

				std::vector<std::u8string> titles{};
				titles.reserve(mv.title.items.size());
				for (auto const& [_, title] : mv.title.items) {
					titles.push_back(title.text);
				}
				for (auto& s : titles) {
					for (auto& c : s) {
						c = std::tolower(static_cast<unsigned char>(c));
					}
				}

				for (auto const& video : videos) {
					auto video_key = video;
					for (auto& c : video_key) {
						if (c == '-' || c == '_') c = ' ';
					}

					auto ratio = similar(info_key, video_key);
					for (auto const& title : titles) {
						auto const next_ratio = similar(title, video_key);
						ratio = (std::max)(ratio, next_ratio);
					}

					// This trully is a magic number
					if (ratio < 0.8) continue;  // NOLINT

					close_calls.push_back({ratio, video, info});
				}
			}

			std::sort(close_calls.begin(), close_calls.end(),
			          std::greater<diff>{});
			std::unordered_set<string> used_infos;
			std::unordered_set<string> used_videos;

			vector<diff> result{};
			for (auto& D : close_calls) {
				if (used_infos.count(D.info) || used_videos.count(D.video))
					continue;
				used_infos.insert(D.info);
				used_videos.insert(D.video);
				result.push_back(std::move(D));
			}

			std::erase_if(infos, [&used_infos](string const& s) {
				return used_infos.count(s) != 0;
			});
			std::erase_if(videos, [&used_videos](string const& s) {
				return used_videos.count(s) != 0;
			});
			return result;
		}
	}  // namespace

	vector<diff> differ::calc() { return diffs(jsons, infos, videos); }
}  // namespace movies
