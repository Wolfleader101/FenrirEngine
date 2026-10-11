#pragma once

#include "Iris/CallResult.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace Iris::Detail
{
    // Reads the length-prefixed snapshots the bridge produces (see the managed TypeQueries). Every read is
    // bounds-checked; `ok` turns false on the first overrun, so a malformed block becomes InvalidResponse
    // instead of an out-of-range read.
    class SnapshotReader
    {
      public:
        SnapshotReader(const std::uint8_t* data, std::size_t size) noexcept : cursor(data), end(data + size)
        {
        }

        std::int32_t Int32() noexcept
        {
            std::int32_t value = 0;
            Read(&value, sizeof(value));
            return value;
        }
        std::int64_t Int64() noexcept
        {
            std::int64_t value = 0;
            Read(&value, sizeof(value));
            return value;
        }
        double Double() noexcept
        {
            double value = 0;
            Read(&value, sizeof(value));
            return value;
        }
        std::string String()
        {
            const std::int32_t length = Int32();
            if (!ok || length < 0 || static_cast<std::size_t>(end - cursor) < static_cast<std::size_t>(length))
            {
                ok = false;
                return {};
            }
            std::string text(reinterpret_cast<const char*>(cursor), static_cast<std::size_t>(length));
            cursor += length;
            return text;
        }
        // True while nothing overran; at the end it should also have consumed everything.
        [[nodiscard]] bool Finished() const noexcept
        {
            return ok && cursor == end;
        }
        // Marks the data malformed (an unknown tag, say).
        void Fail() noexcept
        {
            ok = false;
        }
        [[nodiscard]] bool Ok() const noexcept
        {
            return ok;
        }

      private:
        void Read(void* destination, std::size_t size) noexcept
        {
            if (!ok || static_cast<std::size_t>(end - cursor) < size)
            {
                ok = false;
                return;
            }
            std::memcpy(destination, cursor, size);
            cursor += size;
        }

        const std::uint8_t* cursor;
        const std::uint8_t* end;
        bool ok = true;
    };
} // namespace Iris::Detail
