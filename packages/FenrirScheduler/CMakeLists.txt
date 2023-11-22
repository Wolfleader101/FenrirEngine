add_library(FenrirScheduler STATIC
    src/Scheduler.cpp
    include/FenrirScheduler/Scheduler.hpp
)

target_include_directories(FenrirScheduler PUBLIC include)