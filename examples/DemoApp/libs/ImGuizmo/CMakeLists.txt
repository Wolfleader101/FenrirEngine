project(imguizmo)

add_library(imguizmo STATIC)

target_include_directories(imguizmo SYSTEM PUBLIC
    imguizmo/
)

file(GLOB SRC_IMGUIZMO imguizmo/*.cpp)
target_sources(imguizmo PRIVATE ${SRC_IMGUIZMO})