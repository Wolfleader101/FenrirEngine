// Must NOT compile: a by-value std::string is not a signature argument (strings are passed as std::string_view).
// The UnsupportedTypeDoesNotCompile CTest builds this file and passes only when the compiler reports
// Iris's own static_assert message, so it cannot pass by failing for an unrelated reason.
#include <Iris/Host.hpp>

#include <string>

void Unsupported(const Iris::Type& type)
{
    (void)type.FindMethod<void(std::string)>("Name");
}
