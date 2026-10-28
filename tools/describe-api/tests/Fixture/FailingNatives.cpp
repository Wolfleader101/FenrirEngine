// Native functions the natives mode refuses, one per diagnostic. The tests describe the file once and check that every
// diagnostic is reported. It is read, never compiled.

#include <cstdint>
#include <string_view>

namespace Host
{
    class CallContext
    {
    };
} // namespace Host

namespace Failing
{
    // Plain data the `returns` cases point at; never described.
    struct Vec3
    {
        float x = 0.0f;
    };

    struct Hit
    {
        float distance = 0.0f;
    };

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

    /** @param[in] value One value, but not a pointer. */
    bool InValue(const Host::CallContext&, int value)
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

    bool TextByPointer(const Host::CallContext&, const std::string_view* text)
    {
        return text != nullptr;
    }

    /** @facade Facades */
    bool MalformedFacade(const Host::CallContext&)
    {
        return false;
    }

    /**
     * @facade Facades.Once
     * @facade Facades.Twice
     */
    bool DoubledFacade(const Host::CallContext&)
    {
        return false;
    }

    /**
     * @param[out] hit Where it hit.
     * @facade Facades.ReturnsMissing returns missing
     */
    bool ReturnsMissing(const Host::CallContext&, Hit* hit)
    {
        return hit != nullptr;
    }

    /**
     * @param[out] hit Where it hit.
     * @facade Facades.ReturnsInput returns origin
     */
    bool ReturnsInput(const Host::CallContext&, const Vec3* origin, Hit* hit)
    {
        return origin != nullptr && hit != nullptr;
    }

    /**
     * @param[out] nearest The nearest hit.
     * @param[out] farthest The farthest hit.
     * @facade Facades.ReturnsOneOfTwo returns nearest
     */
    bool ReturnsOneOfTwo(const Host::CallContext&, Hit* nearest, Hit* farthest)
    {
        return nearest != nullptr && farthest != nullptr;
    }

    /**
     * @param[out] hit Where it hit.
     * @facade Facades.ReturnsBesideCount returns hit
     */
    std::int32_t ReturnsBesideCount(const Host::CallContext&, Hit* hit)
    {
        return hit != nullptr ? 1 : 0;
    }
} // namespace Failing
