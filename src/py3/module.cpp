#include <boost/python.hpp>
#include <cerrno>
#include <io/file.hpp>
#include <movies/movie_info.hpp>
#include <py3/converter.hpp>

namespace movies::v1 {
	bool debug_on = false;
	void set_debug(bool value) {
		if (value != debug_on) {
			std::cerr << "-- debug on: " << (value ? "yes" : "no") << '\n';
		}
		debug_on = value;
	}
	bool get_debug() noexcept { return debug_on; }

	bool movie_info__merge(movie_info& self,
	                       movie_info const& new_data,
	                       prefer_title which_title,
	                       prefer_details which_details) {
		auto const result = self.merge(new_data, which_title, which_details);
		if (result == json::conv_result::failed) {
			fprintf(stderr, "throwing \"failed merging two movies\"\n");
			throw std::runtime_error("failed merging two movies");
		}
		return result == json::conv_result::updated;
	}

	movie_info static__movie_info__loads(string_type const& data) {
		auto node = json::read_json(as_json_view(data));
		std::string dbg;
		movie_info self;
		if (::json::load(node, self, dbg) == ::json::conv_result::failed)
			throw std::runtime_error("failed loading the movie info");
		return self;
	}

	movie_info static__movie_info__load_from(string_type const& path) {
		using namespace boost::python;
		if (debug_on)
			std::cerr << "-- movie_info.load_from(" << as_ascii_view(path)
			          << ")\n";
		auto file = io::file::open(as_fs_view(path), "rb");
		if (!file) {
			auto const view = as_ascii_view(path);
			str path_{view.data(), view.length()};
			errno = ENOENT;
			PyErr_SetFromErrnoWithFilenameObject(PyExc_FileNotFoundError,
			                                     path_.ptr());
			throw_error_already_set();
		}

		auto const bytes = io::contents(file);
		if (debug_on) std::cerr << "-- bytes: " << bytes.size() << '\n';
		auto node = json::read_json({bytes.data(), bytes.size()});
		std::string dbg;
		movie_info self;
		if (::json::load(node, self, dbg) == ::json::conv_result::failed)
			throw std::runtime_error("failed loading the movie info");
		if (debug_on && dbg.length())
			std::cerr << "-- debug:\n\n" << dbg << '\n';
		return self;
	}

	string_type movie_info__as_string(movie_info const& self) {
		auto node = self.to_json();
		std::u8string output;
		json::write_json(output, node, json::four_spaces);
		return as_string(std::move(output));
	}

	void movie_info__store_at(movie_info const& self, string_type const& path) {
		using namespace boost::python;
		auto file = io::file::open(as_fs_view(path), "wb");
		if (!file) {
			auto const view = as_ascii_view(path);
			str path_{view.data(), view.length()};
			errno = ENOENT;
			PyErr_SetFromErrnoWithFilenameObject(PyExc_FileNotFoundError,
			                                     path_.ptr());
			throw_error_already_set();
		}

		json::write_json(file.get(), self.to_json(), json::four_spaces);
	}

	json::node simpler(json::node value, int level);
	struct simplifier {
		int level;

		template <typename T>
		json::node operator()(T const& value) const {
			return json::node{value};
		}

		json::node operator()(json::string const& value) const {
			size_t pos = std::min(size_t{100u}, value.length());
			while (pos < value.length()) {
				auto c = static_cast<unsigned char>(value[pos]);
				if (c < 0x80) break;
				++pos;
			}
			if (pos == value.length()) return value;
			return value.substr(0, pos) + u8"...";
		}

		json::node operator()(json::map const& values) const {
			json::map result{};
			if (level < 1) return result;
			for (auto const& [key, value] : values) {
				result[key] = simpler(value, level - 1);
			}
			return result;
		}

		json::node operator()(json::array const& values) const {
			json::array result{};
			if (level < 1) return result;
			result.reserve(values.size());
			for (auto const& value : values) {
				result.push_back(simpler(value, level - 1));
			}
			return result;
		}
	};

	json::node simpler(json::node value, int level) {
		return std::visit(simplifier{level}, value.base());
	}

	void setup_api();

#define CREW_CAT_X(X) \
	X(directors)      \
	X(writers)        \
	X(cast)

	using namespace boost::python;

	struct crew_builder {
		enum class cat {
#define X_DECL_TYPE(NAME) NAME,
			CREW_CAT_X(X_DECL_TYPE)
#undef X_DECL_TYPE
		};

		std::map<long long, std::vector<string_type>> refs;
		std::vector<string_type> names;
		std::map<string_type, long long> rev_names;
		std::map<cat, std::vector<role_info>> crew;

		void add(cat kind,
		         string_type const& full_name,
		         std::optional<string_type> const& ref,
		         std::optional<string_type> const& contribution) {
			long long id{};

			auto it = rev_names.lower_bound(full_name);
			if (it == rev_names.end() || it->first != full_name) {
				id = names.size();
				names.push_back(full_name);
				rev_names.insert(it, {full_name, id});
			} else {
				id = it->second;
			}

			if (ref) {
				auto found = false;
				auto& refs_ = refs[id];
				for (auto const& ref_ : refs_) {
					if (ref_ == *ref) {
						found = true;
						break;
					}
				}

				if (!found) refs_.push_back(*ref);
			}

			crew[kind].push_back({.id = id, .contribution = contribution});
		}

		void apply(movie_info& info) {
			info.crew = crew_info{};
			long long id{};

			for (auto& name : names) {
				auto it = refs.find(id);
				++id;

				info.crew.names.push_back({});
				auto& names_ = info.crew.names.back();
				names_.name = std::move(name);
				if (it == refs.end()) continue;
				names_.refs = std::move(it->second);
			}

			for (auto& [kind, roles] : crew) {
				auto ptr = [kind,
				            crew = &info.crew]() -> std::vector<role_info>* {
					switch (kind) {
						case cat::directors:
							return &crew->directors;
						case cat::writers:
							return &crew->writers;
						case cat::cast:
							return &crew->cast;
					}
					return nullptr;
				}();
				if (!ptr) continue;
				*ptr = std::move(roles);
			}
		}
	};
}  // namespace movies::v1

BOOST_PYTHON_MODULE(movies) {
	using namespace boost::python;
	using namespace movies;

	converter::initialize_movies_converters();
	v1::setup_api();

	{
		auto outer = scope{class_<crew_builder>("crew_builder")
		                       .def("add", &crew_builder::add)
		                       .def("apply", &crew_builder::apply)};

		enum_<crew_builder::cat>("cat")
#define X_VALUE(NAME) .value(#NAME, crew_builder::cat::NAME)
		    CREW_CAT_X(X_VALUE)
#undef X_VALUE
		        ;
	}

	{
		scope current;
		api::setattr(current, "version", movies::VERSION);
	}
	def("set_debug", set_debug);
	def("get_debug", get_debug);
}
