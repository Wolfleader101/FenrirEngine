add_library(FenrirTime STATIC
    src/Time.cpp
    include/FenrirTime/Time.hpp
)
target_compile_features(FenrirTime PUBLIC cxx_std_20)

target_include_directories(FenrirTime PUBLIC include)