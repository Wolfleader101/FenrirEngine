add_library(FenrirApp STATIC
    src/App.cpp
    include/FenrirApp/App.hpp
)

target_include_directories(FenrirApp PUBLIC include)