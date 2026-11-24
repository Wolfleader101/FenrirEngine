add_library(FenrirScheduler STATIC
    src/Scheduler.cpp
    include/FenrirScheduler/Scheduler.hpp

    src/ThreadPool.cpp
    include/FenrirScheduler/ThreadPool.hpp
)
target_compile_features(FenrirScheduler PUBLIC cxx_std_20)

target_include_directories(FenrirScheduler PUBLIC include)