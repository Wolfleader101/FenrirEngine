# C# scripting for FenrirEngine, built on Iris. Added to the build only when FENRIR_ENABLE_SCRIPTING is on.
add_library(FenrirScripting STATIC
    src/Scripting.cpp
    include/FenrirScripting/Scripting.hpp
)

target_include_directories(FenrirScripting PUBLIC include)
target_link_libraries(FenrirScripting PUBLIC FenrirApp FenrirLogger Iris::Iris)

add_subdirectory(Tests)
