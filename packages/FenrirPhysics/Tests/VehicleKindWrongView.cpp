// Reading a VehicleKind as a kind it does not hold asserts. An assert ends the process, so this is a program of its
// own: CTest runs it in a debug build and passes when it prints the assert's message.

#ifdef _WIN32
#include <stdlib.h>
#else
#include <csignal>
#include <cstdlib>
#endif

#include "FenrirPhysics/Vehicles.hpp"

int main()
{
#ifdef _WIN32
    // The message is printed to stderr; without this the debug runtime also opens a dialog and waits.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    // Elsewhere abort ends the process by SIGABRT, which CTest fails whatever was printed; exit with a status instead.
    std::signal(SIGABRT, [](int) { std::_Exit(EXIT_FAILURE); });
#endif
    Fenrir::VehicleKind kind = Fenrir::MotorcycleVehicle{};
    [[maybe_unused]] const Fenrir::WheeledVehicle& wheeled = kind.As<Fenrir::WheeledVehicle>();
    return 0;
}
