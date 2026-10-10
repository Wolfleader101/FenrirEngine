// Must NOT compile: std::string is not a PrimitiveValue, so it cannot be a managed parameter type.
// The UnsupportedTypeDoesNotCompile CTest builds this file and passes only when the compiler reports
// Iris's own static_assert message, so it cannot pass by failing for an unrelated reason.
#include <Iris/Host.hpp>

#include <string>

void Unsupported(const Iris::Type& type)
{
    (void)type.FindMethod<void(std::string)>("Name");
}
