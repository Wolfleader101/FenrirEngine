add_library(FenrirTime STATIC
    src/Time.cpp
    include/FenrirTime/Time.hpp
)

target_include_directories(FenrirTime PUBLIC include)