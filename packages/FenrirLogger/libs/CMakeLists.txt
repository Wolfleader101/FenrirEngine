set(BUILD_SHARED_LIBS OFF)
set(SPDLOG_BUILD_SHARED OFF)

add_subdirectory(spdlog)

target_include_directories(FenrirLogger PUBLIC
    spdlog/include
)