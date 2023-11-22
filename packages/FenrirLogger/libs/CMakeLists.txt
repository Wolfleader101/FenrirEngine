add_subdirectory(spdlog)

target_include_directories(FenrirLogger PUBLIC
    spdlog/include
)