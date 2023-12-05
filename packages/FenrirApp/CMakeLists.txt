add_library(FenrirApp STATIC
    src/App.cpp
    include/FenrirApp/App.hpp
)

# link against other interal libraries
target_link_libraries(FenrirApp PUBLIC FenrirScheduler FenrirTime FenrirLogger FenrirECS)

target_include_directories(FenrirApp PUBLIC include)