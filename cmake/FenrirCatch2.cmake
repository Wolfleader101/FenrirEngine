# Makes the Catch2::Catch2 and Catch2::Catch2WithMain targets available, once, whoever asks first. The tests of every package
# use it. Catch2 comes from the libs/catch2 submodule (git submodule update --init libs/catch2); when that is not
# checked out, or this file is used outside the repository, an installed Catch2 v3 is found instead.
include_guard(GLOBAL)

function(fenrir_add_catch2)
    if(TARGET Catch2::Catch2)
        return()
    endif()
    set(catch2_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../libs/catch2")
    if(EXISTS "${catch2_source}/CMakeLists.txt")
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        # Set in this function only. Static, so a test executable needs no DLL beside it; and built with the C++ standard
        # the tests use, because Catch2 offers std::string_view and friends only when it was compiled with C++17 or later.
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_CXX_STANDARD 20)
        add_subdirectory("${catch2_source}" "${CMAKE_BINARY_DIR}/catch2")
    else()
        find_package(Catch2 3 REQUIRED)
    endif()
endfunction()
