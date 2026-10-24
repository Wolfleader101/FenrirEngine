# FenrirInput's tests: input fed through the input feed with no window, the App driven one frame at a time with a
# chosen delta (seam 3 of the renderer foundation spec).
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirInputTests InputTestApp.hpp KeyboardEdges.cpp Keys.cpp MouseInput.cpp GamepadInput.cpp
                                InputEvents.cpp Installs.cpp Dispatch.cpp TestRouter.hpp
                                Routers.cpp ControlOwners.cpp InputClaims.cpp)
target_link_libraries(FenrirInputTests PRIVATE FenrirInput Catch2::Catch2WithMain)
add_test(NAME FenrirInput COMMAND FenrirInputTests)
