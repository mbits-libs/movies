#pragma once

#include <date/date.h>
#include <fmt/format.h>
#include <boost/python/class.hpp>
#include <boost/python/converter/builtin_converters.hpp>
#include <boost/python/converter/registry.hpp>
#include <boost/python/converter/return_from_python.hpp>
#include <boost/python/dict.hpp>
#include <boost/python/enum.hpp>
#include <boost/python/make_constructor.hpp>
#include <boost/python/object.hpp>
#include <boost/python/scope.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/tuple.hpp>
#include <optional>
#include <filesystem>

#define BOOST_PYTHON_RETURN_TO_PYTHON_BY_VALUE(T, expr, pytype)            \
	template <>                                                            \
	struct to_python_value<T&> : detail::builtin_to_python {               \
		inline PyObject* operator()(T const& x) const { return (expr); }   \
		inline PyTypeObject const* get_pytype() const { return (pytype); } \
	};                                                                     \
	template <>                                                            \
	struct to_python_value<T const&> : detail::builtin_to_python {         \
		inline PyObject* operator()(T const& x) const { return (expr); }   \
		inline PyTypeObject const* get_pytype() const { return (pytype); } \
	};

#define BOOST_PYTHON_ARG_TO_PYTHON_BY_VALUE(T, expr)              \
	namespace converter {                                         \
		template <>                                               \
		struct arg_to_python<T> : handle<> {                      \
			arg_to_python(T const& x) : python::handle<>(expr) {} \
		};                                                        \
	}

// Specialize argument and return value converters for T using expr
#define BOOST_PYTHON_TO_PYTHON_BY_VALUE(T, expr, pytype)    \
	BOOST_PYTHON_RETURN_TO_PYTHON_BY_VALUE(T, expr, pytype) \
	BOOST_PYTHON_ARG_TO_PYTHON_BY_VALUE(T, expr)

namespace boost::python {
	inline auto PyUnicode_from_view(std::u8string_view view) {
		return ::PyUnicode_FromStringAndSize(
		    reinterpret_cast<char const*>(view.data()),
		    implicit_cast<ssize_t>(view.size()));
	}

	inline auto PyUnicode_from_path(std::filesystem::path const& path) {
		return PyUnicode_from_view(path.generic_u8string());
	}

	inline auto PyLong_from_sys_seconds(date::sys_seconds const& timestamp) {
		return ::PyLong_FromLongLong(timestamp.time_since_epoch().count());
	}

	BOOST_PYTHON_TO_PYTHON_BY_VALUE(std::u8string,
	                                PyUnicode_from_view(x),
	                                &PyUnicode_Type);
	BOOST_PYTHON_TO_PYTHON_BY_VALUE(std::filesystem::path,
	                                PyUnicode_from_path(x),
	                                &PyUnicode_Type);
	BOOST_PYTHON_TO_PYTHON_BY_VALUE(date::sys_seconds,
	                                PyLong_from_sys_seconds(x),
	                                &PyLong_Type);

	template <typename T>
	struct to_python_value<std::optional<T>&> : python::to_python_value<T&> {
		inline PyObject* operator()(std::optional<T> const& x) const {
			if (x) return python::to_python_value<T&>::operator()(*x);
			return python::incref(Py_None);
		}
	};
	template <typename T>
	struct to_python_value<std::optional<T> const&>
	    : python::to_python_value<T const&> {
		inline PyObject* operator()(std::optional<T> const& x) const {
			if (x) return python::to_python_value<T const&>::operator()(*x);
			return python::incref(Py_None);
		}
	};

	template <typename Key, typename Value, bool NoProxy = true>
	class map_ : public class_<std::map<Key, Value>> {
	public:
		using class_<std::map<Key, Value>>::class_;

		map_<Key, Value, NoProxy>& def_map() {
			this->def(map_indexing_suite<std::map<Key, Value>, NoProxy>{});
			return *this;
		}
	};

	template <typename Value, bool NoProxy = true>
	class vector_ : public class_<std::vector<Value>> {
	public:
		using class_<std::vector<Value>>::class_;

		vector_<Value, NoProxy>& def_vector() {
			this->def(vector_indexing_suite<std::vector<Value>, NoProxy>{});
			return *this;
		}
	};

	template <typename Translatable, bool NoProxy = true>
	class translating_suite
	    : public def_visitor<translating_suite<Translatable>> {
	public:
		using value_type = typename Translatable::value_type;
		using map_type = typename Translatable::map_t;
		using const_iterator = typename Translatable::const_iterator;

		template <class Class>
		void visit(Class& cl) const {
			object class_name(cl.attr("__name__"));
			extract<std::string> class_name_extractor(class_name);
			auto items_name = class_name_extractor();
			items_name += "_items";
			map_<std::string, value_type, NoProxy>(items_name.c_str())
			    .def_map();

			cl.def_readonly("items", &Translatable::items)
			    .def("find", &find_one)
			    .def("update", &Translatable::update);
		}

	private:
		static std::optional<value_type> find_one(Translatable& translatable,
		                                          std::string const& key) {
			auto it = translatable.find(key);
			if (it == translatable.end()) return std::nullopt;
			return it->second;
		}
	};

	template <typename Value, bool NoProxy = true>
	class translatable_ : public class_<movies::translatable<Value>> {
	public:
		using class_<movies::translatable<Value>>::class_;

		translatable_<Value, NoProxy>& def_tr() {
			this->def(
			    translating_suite<movies::translatable<Value>, NoProxy>{});
			return *this;
		}
	};

	namespace converter {
		template <typename T>
		struct arg_to_python<std::optional<T>> : handle<> {
			arg_to_python(std::optional<T> const& x)
			    : python::handle<>(x ? arg_to_python<T>(*x) : object{}) {}
		};

		template <class T>
		struct slot_optional_rvalue_from_python {
		public:
			slot_optional_rvalue_from_python() {
				auto const inner = registry::query(type_id<T>());
				if (!inner) return;  // TODO: or throw?
				registry::insert(
				    &slot_optional_rvalue_from_python<T>::convertible,
				    &slot_optional_rvalue_from_python<T>::construct,
				    type_id<std::optional<T>>(),
				    inner->m_to_python_target_type);
			}

		private:
			static void* py_none_unaryfunc(PyObject*) {
				return python::incref(Py_None);
			};
			static void* convertible(PyObject* obj) {
				auto const inner = registry::query(type_id<T>());
				if (!inner) return nullptr;
				if (obj == Py_None)
					return reinterpret_cast<void*>(&py_none_unaryfunc);
				return inner->rvalue_chain->convertible(obj);
			}

			static void construct(PyObject* obj,
			                      rvalue_from_python_stage1_data* data) {
				if (obj == Py_None) return construct_nullopt(data);
				return_from_python<T> converter;

				// Get the location in which to construct
				void* storage =
				    ((rvalue_from_python_storage<T>*)data)->storage.bytes;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
				new (storage) std::optional<T>(converter(incref(obj)));

#ifdef _MSC_VER
#pragma warning(pop)
#endif
				// record successful construction
				data->convertible = storage;
			}

			static void construct_nullopt(
			    rvalue_from_python_stage1_data* data) {
				void* storage =
				    ((rvalue_from_python_storage<std::optional<T>>*)data)
				        ->storage.bytes;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
				new (storage) std::optional<T>(std::nullopt);

#ifdef _MSC_VER
#pragma warning(pop)
#endif
				// record successful construction
				data->convertible = storage;
			}
		};

		void initialize_movies_converters();
	}  // namespace converter
}  // namespace boost::python

#undef BOOST_PYTHON_RETURN_TO_PYTHON_BY_VALUE
#undef BOOST_PYTHON_ARG_TO_PYTHON_BY_VALUE
#undef BOOST_PYTHON_TO_PYTHON_BY_VALUE
