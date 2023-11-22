add_library(FenrirLogger STATIC
    src/ILogger.cpp
    include/FenrirLogger/ILogger.hpp
    src/ConsoleLogger.cpp
    include/FenrirLogger/ConsoleLogger.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirLogger PUBLIC spdlog)

target_include_directories(FenrirLogger PUBLIC include)