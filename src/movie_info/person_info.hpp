// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

namespace movies::v1 {
	namespace {
#define TWC(fld) \
	if (auto cmp = lhs.fld <=> rhs.fld; cmp != 0) return cmp < 0

		struct person_info_t {
			std::optional<std::u8string> name;
			std::map<std::u8string, std::u8string> refs;
			std::optional<std::u8string> contribution;
			size_t original_position{};
			bool from_existing{};

			person_info_t merge(person_info_t const& new_data) && {
				// names must be equal
				for (auto const& [key, value] : new_data.refs) {
					auto it = refs.lower_bound(key);
					if (it != refs.end() && it->first == key) continue;
					refs.insert(it, {key, value});
				}
				contribution = new_data.contribution || contribution;
				return std::move(*this);
			}

			long long add_to(std::vector<person_name>& used) const {
				if (!name) {
					return (std::numeric_limits<long long>::max)();
				}
				std::vector<std::u8string> reflist{};
				reflist.reserve(refs.size());
				for (auto const& [key, value] : refs) {
					if (value.empty()) {
						reflist.push_back(key);
					} else {
						std::u8string ref{};
						ref.reserve(key.length() + value.length() + 1);
						ref.append(key);
						ref.push_back(':');
						ref.append(value);
						reflist.push_back(ref);
					}
				}

				long long index{};
				for (auto const& info : used) {
					if (info.name == *name && info.refs == reflist)
						return index;
					++index;
				}

				used.emplace_back(*name, std::move(reflist));
				return index;
			}

			static auto find_in(std::vector<person_name> const& list,
			                    long long index) {
				if (index < 0) return list.end();
				auto const uindex = static_cast<size_t>(index);
				return uindex >= list.size()
				           ? list.end()
				           : std::next(list.begin(),
				                       static_cast<ptrdiff_t>(uindex));
			}

			static std::vector<person_info_t> conv(
			    std::vector<role_info> const& list,
			    std::vector<person_name> const& names,
			    bool from_existing) {
				std::vector<person_info_t> result{};
				result.resize(list.size());
				for (size_t index = 0; index < list.size(); ++index) {
					auto& dst = result[index];
					auto const& src = list[index];
					auto it = find_in(names, src.id);

					if (it != names.end()) {
						dst.name = it->name;
						for (auto const& ref : it->refs) {
							auto const view = std::u8string_view{ref};
							auto const pos = ref.find(':');
							if (pos == std::u8string::npos) {
								dst.refs[ref].clear();
							} else {
								dst.refs[ref.substr(0, pos)] =
								    ref.substr(pos + 1);
							}
						}
					}

					dst.contribution = src.contribution;
					dst.original_position = index;
					dst.from_existing = from_existing;
				}
				std::erase_if(result, [](person_info_t const& person) {
					return !person.name;
				});

				std::stable_sort(
				    result.begin(), result.end(),
				    [](person_info_t const& lhs,
				       person_info_t const& rhs) -> bool {
					    TWC(name);
					    TWC(contribution);
					    TWC(refs);
					    if (lhs.original_position != rhs.original_position)
						    return lhs.original_position <
						           rhs.original_position;
					    if (lhs.from_existing == rhs.from_existing)
						    return false;
					    return !lhs.from_existing;
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
						              new_data.begin() +
						                  static_cast<ptrdiff_t>(index_new),
						              new_data.end());
						index_new = size_new;
						continue;
					}

					if (index_new == size_new) {
						result.insert(result.end(),
						              old_data.begin() +
						                  static_cast<ptrdiff_t>(index_old),
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
						    return !lhs.from_existing;
					    if (lhs.original_position != rhs.original_position)
						    return lhs.original_position <
						           rhs.original_position;
					    TWC(name);
					    TWC(contribution);
					    TWC(refs);
					    return false;
				    });

				return result;
			}
		};

#undef TWC
	}  // namespace
}  // namespace movies::v1
