add_library(FenrirCamera STATIC
    src/Camera.cpp
    include/FenrirCamera/Camera.hpp
)

# link against other interal libraries
target_link_libraries(FenrirCamera PUBLIC FenrirMath)

target_include_directories(FenrirCamera PUBLIC include)