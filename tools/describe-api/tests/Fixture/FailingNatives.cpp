// Native functions the natives mode refuses, one per diagnostic. The tests describe the file once and check that every
// diagnostic is reported. It is read, never compiled.

namespace Host
{
    class CallContext
    {
    };
} // namespace Host

namespace Failing
{
    namespace
    {
        bool Hidden(const Host::CallContext&)
        {
            return false;
        }
    } // namespace

    bool ByReference(const Host::CallContext&, int& value)
    {
        return value != 0;
    }

    bool Unnamed(const Host::CallContext&, int)
    {
        return false;
    }

    /** @param[out] value Not a pointer. */
    bool OutValue(const Host::CallContext&, int value)
    {
        return value != 0;
    }

    /** @param[in,out] values Read and written, but const. */
    bool InOutConst(const Host::CallContext&, const int* values)
    {
        return values != nullptr;
    }

    /** @notrunning 1 */
    void NotRunningVoid(const Host::CallContext&)
    {
    }

    /** @param missing No such parameter. */
    bool DocumentsMissing(const Host::CallContext&, int value)
    {
        return value != 0;
    }

    bool PointerToPointer(const Host::CallContext&, int** values)
    {
        return values != nullptr;
    }

    bool Wide(const Host::CallContext&, long double wide)
    {
        return wide != 0;
    }
} // namespace Failing
