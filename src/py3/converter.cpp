#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <movies/types.hpp>
#include <py3/converter.hpp>

namespace boost::python::converter {
	namespace {
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

		template <class T, class SlotPolicy>
		struct slot_rvalue_from_python {
		public:
			slot_rvalue_from_python() {
				registry::insert(
				    &slot_rvalue_from_python<T, SlotPolicy>::convertible,
				    &slot_rvalue_from_python<T, SlotPolicy>::construct,
				    type_id<T>(), &SlotPolicy::get_pytype);
			}

		private:
			static void* convertible(PyObject* obj) {
				unaryfunc* slot = SlotPolicy::get_slot(obj);
				return slot && *slot ? slot : 0;
			}

			static void construct(PyObject* obj,
			                      rvalue_from_python_stage1_data* data) {
				// Get the (intermediate) source object
				unaryfunc creator = *static_cast<unaryfunc*>(data->convertible);
				handle<> intermediate(creator(obj));

				// Get the location in which to construct
				void* storage =
				    ((rvalue_from_python_storage<T>*)data)->storage.bytes;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
				new (storage) T(SlotPolicy::extract(intermediate.get()));

#ifdef _MSC_VER
#pragma warning(pop)
#endif
				// record successful construction
				data->convertible = storage;
			}
		};

		unaryfunc py_unicode_as_string_unaryfunc = PyUnicode_AsUTF8String;
		unaryfunc py_ident_unaryfunc = python::incref;

		struct u8string_rvalue_from_python {
			// If the underlying object is "string-able" this will succeed
			static unaryfunc* get_slot(PyObject* obj) {
				return (PyUnicode_Check(obj)) ? &py_unicode_as_string_unaryfunc
				                              : 0;
			};

			// Remember that this will be used to construct the result
			// object
			static std::u8string extract(PyObject* intermediate) {
				return std::u8string(reinterpret_cast<char8_t const*>(
				                         PyBytes_AsString(intermediate)),
				                     PyBytes_Size(intermediate));
			}
			static PyTypeObject const* get_pytype() { return &PyUnicode_Type; }
		};

		struct sys_seconds_rvalue_from_python {
			static unaryfunc* get_slot(PyObject* obj) {
				return (PyLong_Check(obj)) ? &py_ident_unaryfunc : 0;
			};

			// Remember that this will be used to construct the result
			// object
			static date::sys_seconds extract(PyObject* intermediate) {
				auto const raw = PyLong_AsLongLong(intermediate);
				return date::sys_seconds{std::chrono::seconds{raw}};
			}
			static PyTypeObject const* get_pytype() { return &PyLong_Type; }
		};
	}  // namespace

	void initialize_movies_converters() {
		slot_rvalue_from_python<std::u8string, u8string_rvalue_from_python>{};
		slot_rvalue_from_python<date::sys_seconds,
		                        sys_seconds_rvalue_from_python>{};

#define REGISTER_INT_OPTIONAL(INT)                  \
	slot_optional_rvalue_from_python<signed INT>{}; \
	slot_optional_rvalue_from_python<unsigned INT>{};
		REGISTER_INT_OPTIONAL(char);
		REGISTER_INT_OPTIONAL(short);
		REGISTER_INT_OPTIONAL(int);
		REGISTER_INT_OPTIONAL(long);
		REGISTER_INT_OPTIONAL(long long);
		slot_optional_rvalue_from_python<float>{};
		slot_optional_rvalue_from_python<double>{};
		slot_optional_rvalue_from_python<long double>{};
		slot_optional_rvalue_from_python<date::sys_seconds>{};
		slot_optional_rvalue_from_python<std::u8string>{};
		slot_optional_rvalue_from_python<std::string>{};

		vector_<movies::string_type>("vector_str").def_vector();

		translatable_<movies::string_type>("translatable_str").def_tr();
	}
}  // namespace boost::python::converter
