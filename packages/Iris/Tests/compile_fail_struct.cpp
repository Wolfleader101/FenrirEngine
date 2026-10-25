// Must NOT compile: a struct of two fields is not an internal call parameter by value (it crosses by pointer; only a
// struct of one field with an Iris::WrapperOf specialization crosses by value). The StructByValueDoesNotCompile CTest
// builds this file and passes only when the compiler reports Iris's own static_assert message, so it cannot pass by
// failing for an unrelated reason.
#include <Iris/InternalCall.hpp>

#include <cstdint>

struct TwoFields
{
    std::int32_t left;
    std::int32_t right;
};

std::int32_t Sum(TwoFields both)
{
    return both.left + both.right;
}

void Unsupported()
{
    (void)Iris::RegisterInternalCall<&Sum>("CompileFail.Natives.Sum");
}
