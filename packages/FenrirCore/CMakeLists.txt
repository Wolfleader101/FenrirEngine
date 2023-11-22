add_library(FenrirCore STATIC
    src/FenrirCore.cpp
    include/FenrirCore/FenrirCore.hpp
)

target_include_directories(FenrirCore PUBLIC include)