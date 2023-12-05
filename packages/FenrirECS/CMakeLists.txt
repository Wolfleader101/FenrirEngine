add_library(FenrirECS STATIC
    include/FenrirECS/Entity.hpp
    src/Entity.cpp

    include/FenrirECS/EntityList.hpp
    src/EntityList.cpp

    include/FenrirECS/DefaultComponents.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirECS PUBLIC FenrirMath)

target_include_directories(FenrirECS PUBLIC include)