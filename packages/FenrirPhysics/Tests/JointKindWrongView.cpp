// Reading a JointKind as a kind it does not hold asserts. An assert ends the process, so this is a program of its own:
// CTest runs it in a debug build and passes when it prints the assert's message.

#ifdef _WIN32
#include <stdlib.h>
#endif

#include "FenrirPhysics/Joints.hpp"

int main()
{
#ifdef _WIN32
    // The message is printed to stderr; without this the debug runtime also opens a dialog and waits.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    Fenrir::JointKind kind = Fenrir::BallJoint{};
    [[maybe_unused]] const Fenrir::HingeJoint& hinge = kind.As<Fenrir::HingeJoint>();
    return 0;
}
