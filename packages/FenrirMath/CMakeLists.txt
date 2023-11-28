add_library(FenrirMath STATIC
    src/Math.cpp
    include/FenrirMath/Math.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirMath PUBLIC glm)

target_include_directories(FenrirMath PUBLIC include)