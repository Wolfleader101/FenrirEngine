add_library(FenrirApp STATIC
    src/App.cpp
    include/FenrirApp/App.hpp
)

# link against other interal libraries
target_link_libraries(FenrirApp PUBLIC FenrirScheduler FenrirTime FenrirLogger FenrirScene FenrirECS)

target_include_directories(FenrirApp PUBLIC include)
# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
