add_library(FenrirScene STATIC
    include/FenrirScene/Scene.hpp
    
    src/Scene.cpp
)

target_link_libraries(FenrirScene PUBLIC FenrirECS FenrirLogger)

target_include_directories(FenrirScene PUBLIC include)