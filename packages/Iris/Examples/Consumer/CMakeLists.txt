# A standalone project that uses an installed Iris package, the way any C++ project would. It is not
# part of the Iris or FenrirEngine builds. Configure it with CMAKE_PREFIX_PATH pointing at the Iris
# install prefix.
cmake_minimum_required(VERSION 3.20)
project(IrisConsumer LANGUAGES CXX)

find_package(Iris 0.2 CONFIG REQUIRED)

add_executable(IrisConsumer main.cpp)
target_compile_features(IrisConsumer PRIVATE cxx_std_20)
target_link_libraries(IrisConsumer PRIVATE Iris::Iris)
# Copies nethost and the bridge next to the executable.
iris_deploy(IrisConsumer)
