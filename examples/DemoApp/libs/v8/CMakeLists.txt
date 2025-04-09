add_library(v8lib INTERFACE)

# Path to actual V8 root
set(V8_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/v8)

# Include headers
target_include_directories(v8lib INTERFACE ${V8_ROOT}/include)

# Link directories
target_link_directories(v8lib INTERFACE
    $<$<CONFIG:Debug>:${V8_ROOT}/lib/Debug>
    $<$<CONFIG:Release>:${V8_ROOT}/lib/Release>
)
# target_link_directories(v8lib INTERFACE ${V8_ROOT}/lib/Release)

# Link all required static libs
target_link_libraries(v8lib INTERFACE
    third_party_abseil-cpp_absl.dll.lib
    third_party_icu_icui18n.dll.lib
    third_party_zlib.dll.lib
    v8_libbase.dll.lib
    v8_libplatform.dll.lib
    v8.dll.lib
)
