add_library(FenrirScheduler STATIC
    src/Scheduler.cpp
    include/FenrirScheduler/Scheduler.hpp

    src/ThreadPool.cpp
    include/FenrirScheduler/ThreadPool.hpp
)

target_include_directories(FenrirScheduler PUBLIC include)