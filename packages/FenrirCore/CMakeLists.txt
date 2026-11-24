add_library(FenrirCore STATIC
    src/FenrirCore.cpp
    include/FenrirCore/FenrirCore.hpp
)
target_compile_features(FenrirCore PUBLIC cxx_std_20)

target_include_directories(FenrirCore PUBLIC include)