// The natives mode's fixture: native functions as a binding's extension defines them, one of each form of parameter a
// natives description records, among functions that are not natives. Fixture.binding.toml's [natives] names this file;
// Fixture.natives.json is its description. It is read, never compiled.

#include <cstdint>
#include <string_view>

#include "Fixture.hpp"

namespace Host
{
    /** @brief What every native function takes first. */
    class CallContext
    {
      public:
        /** @brief Takes the call context first, but is a member function: not a native. */
        bool Member(const CallContext& other) const;
    };

    /** @brief An extension's state, which a native may take second. */
    struct State
    {
        int calls = 0;
    };
} // namespace Host

namespace Fixture
{
    /** @brief An entity as a native takes it: an alias, which a binding may map to a type of its own. */
    using EntityId = std::uint32_t;

    namespace
    {
        // Neither is a native: the first takes no call context, the second is a template.
        int Twice(int value)
        {
            return 2 * value;
        }

        template <typename T>
        bool Templated(const Host::CallContext&, T)
        {
            return false;
        }
    } // namespace

    namespace Natives
    {
        /** @brief Pushes the body of `body` with `force`: an alias, and a const pointer, after the state. */
        bool Push(const Host::CallContext& context, Host::State& state, EntityId body, const Vec3* force)
        {
            (void)context;
            ++state.calls;
            return body != 0 && force != nullptr;
        }

        /**
         * @brief The nearest hit from `origin`.
         *
         * A second paragraph of the function's doc.
         * @param distance How far to look.
         * @param[out] hit Where it hit; the default when it did not.
         * @return Whether it hit.
         */
        bool Find(const Host::CallContext& context, Host::State& state, const Vec3* origin, float distance, Hit* hit)
        {
            (void)context;
            (void)state;
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Hit{};
            return origin != nullptr && distance > 0.0f;
        }

        /**
         * @brief Writes up to `capacity` hits: a stateless native, and a pointer followed by its count.
         * @return How many it wrote.
         */
        std::int32_t FindAll(const Host::CallContext& context, const Vec3* origin, Hit* hits, std::int32_t capacity)
        {
            (void)context;
            (void)origin;
            (void)hits;
            return capacity > 0 ? 0 : capacity;
        }
    } // namespace Natives

    /** @brief Logs text: a native outside the others' namespace, noexcept, taking characters and their length. */
    void Say(const Host::CallContext& context, const char* text, std::int32_t length) noexcept
    {
        (void)context;
        (void)text;
        (void)length;
    }

    /**
     * @brief The address of the next of `body`'s records: a pointer read and written, an untyped pointer, an untyped
     * result, and the value a caller gets when the native cannot be called.
     * @param[in,out] cursor Where to start; moved past the record.
     * @notrunning nullptr
     */
    void* Next(const Host::CallContext& context, std::uint64_t* cursor, const void* body)
    {
        (void)context;
        if (cursor == nullptr)
        {
            return nullptr;
        }
        ++*cursor;
        return const_cast<void*>(body);
    }

    /** @brief Shows `text` in place of `glyph`: text as a std::string_view, and a char32_t, which is a u32. */
    void Show(const Host::CallContext& context, std::string_view text, char32_t glyph)
    {
        (void)context;
        (void)text;
        (void)glyph;
    }

    /** @brief The id after `body`, moving by `motion`: an enum, and a struct of one field, by value. */
    BodyId Advance(const Host::CallContext& context, BodyId body, Motion motion)
    {
        (void)context;
        return BodyId{body.value + static_cast<std::uint32_t>(motion)};
    }

    /** @brief How `body` moves: an enum result. */
    Motion MotionOf(const Host::CallContext& context, BodyId body)
    {
        (void)context;
        return body.value == 0 ? Motion::Still : Motion::Simulated;
    }

    /** @brief The ticket after `ticket`: a bound type of one member, by value. */
    Ticket Renew(const Host::CallContext& context, Ticket ticket)
    {
        (void)context;
        return ticket;
    }

    /** @brief Sums `count` values of numbers' pointee and kinds: a bool, a double, and a 64-bit result. */
    std::uint64_t Sum(const Host::CallContext& context, const std::uint32_t* values, std::int32_t count, bool all,
                      double scale)
    {
        (void)context;
        std::uint64_t sum = 0;
        for (std::int32_t index = 0; index < count && (all || index == 0); ++index)
        {
            sum += values[index];
        }
        return static_cast<std::uint64_t>(static_cast<double>(sum) * scale) + static_cast<std::uint64_t>(Twice(0));
    }
} // namespace Fixture
