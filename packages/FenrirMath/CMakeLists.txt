add_library(FenrirMath STATIC
    src/Math.cpp
    include/FenrirMath/Math.hpp
    include/FenrirMath/Math_fwd.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirMath PUBLIC glm)

target_include_directories(FenrirMath PUBLIC include)