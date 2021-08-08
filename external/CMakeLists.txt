set(JSON_TESTING OFF CACHE BOOL "Compile and/or run self-tests")

add_subdirectory(json)

set_target_properties(json PROPERTIES FOLDER libs)