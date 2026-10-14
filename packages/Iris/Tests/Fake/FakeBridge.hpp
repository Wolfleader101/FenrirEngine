#pragma once

// A stand-in for the managed bridge that lives in the test process: a bridge function table whose slots are C++
// functions a test scripts. It stands where the real bridge (and the managed FixtureBridge) stand, so any response the
// bridge could give, good or malformed, reaches Iris's native modules with no .NET runtime.
//
// The table's slots are plain function pointers with no context, so one fake is active at a time: constructing one
// makes it the active fake, destroying it ends that. A component made from a fake must be destroyed before the fake,
// because destroying a component releases its token through the table.
#include "BridgeFunctions.hpp"
#include "Iris/Component.hpp"
#include "TestAccess.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Iris::Testing
{
    class FakeBridge;
    inline FakeBridge*& ActiveFake() noexcept
    {
        static FakeBridge* active = nullptr;
        return active;
    }

// Every slot of the table, in the table's own order, so the fake can be built from one list.
// clang-format off
#define IRIS_FAKE_SLOTS(X) \
    X(load) \
    X(copyName) \
    X(release) \
    X(getUnloadState) \
    X(forgetUnload) \
    X(registerShared) \
    X(findType) \
    X(copyTypeName) \
    X(findMethod) \
    X(invokeMethod) \
    X(setLogSink) \
    X(forceRelease) \
    X(findInstanceMethod) \
    X(invokeInstanceMethod) \
    X(createObject) \
    X(releaseObject) \
    X(findMember) \
    X(getMember) \
    X(setMember) \
    X(freeString) \
    X(registerStruct) \
    X(createArray) \
    X(arrayLength) \
    X(arrayGet) \
    X(arraySet) \
    X(arrayCopy) \
    X(registerInternalCall) \
    X(listTypes) \
    X(queryType) \
    X(isTypeAssignable) \
    X(waitForUnload)
    // clang-format on

    /** @brief Builds the bytes of a length-prefixed snapshot the way the managed bridge does (little-endian, UTF-8). */
    class SnapshotWriter
    {
      public:
        SnapshotWriter& Int32(std::int32_t value)
        {
            Append(&value, sizeof(value));
            return *this;
        }
        SnapshotWriter& Int64(std::int64_t value)
        {
            Append(&value, sizeof(value));
            return *this;
        }
        SnapshotWriter& String(std::string_view text)
        {
            Int32(static_cast<std::int32_t>(text.size()));
            Append(text.data(), text.size());
            return *this;
        }
        /** @brief A string whose length prefix says `claimed` bytes but whose bytes are `text`. */
        SnapshotWriter& LyingString(std::int32_t claimed, std::string_view text)
        {
            Int32(claimed);
            Append(text.data(), text.size());
            return *this;
        }
        SnapshotWriter& Raw(std::uint8_t byte)
        {
            m_bytes.push_back(byte);
            return *this;
        }
        [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const noexcept
        {
            return m_bytes;
        }

      private:
        void Append(const void* data, std::size_t size)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            m_bytes.insert(m_bytes.end(), bytes, bytes + size);
        }
        std::vector<std::uint8_t> m_bytes;
    };

    class FakeBridge
    {
      public:
        using Table = Iris::Detail::BridgeFunctions;

// One std::function per slot, with the slot's own signature. A test replaces the ones it is about.
#define IRIS_FAKE_DECLARE(field) std::function<std::remove_pointer_t<decltype(Table::field)>> field;
        IRIS_FAKE_SLOTS(IRIS_FAKE_DECLARE)
#undef IRIS_FAKE_DECLARE

        /** @brief Makes this the active fake, fills the table, and gives every slot a benign default (see below). */
        FakeBridge();
        ~FakeBridge()
        {
            ActiveFake() = nullptr;
        }
        FakeBridge(const FakeBridge&) = delete;
        FakeBridge& operator=(const FakeBridge&) = delete;

        /** @brief A loaded component whose calls go to this fake under `token`. Destroy it before the fake. */
        [[nodiscard]] Iris::Component MakeComponent(std::uint64_t token = 1) const
        {
            return Iris::Detail::TestAccess::MakeComponent(m_table, token);
        }

        /** @brief Counts a call to slot number `index` (the trampolines in the table call it). */
        void NoteCall(std::size_t index) noexcept
        {
            ++m_calls[index];
        }

        /** @brief How many times the slot with this managed name (for example "QueryType") was called. */
        [[nodiscard]] int Calls(std::string_view managedName) const
        {
            for (std::size_t index = 0; index < Iris::Detail::BridgeFunctionNames.size(); ++index)
            {
                if (managedName == Iris::Detail::BridgeFunctionNames[index])
                {
                    return m_calls[index];
                }
            }
            throw std::invalid_argument("The bridge has no slot named " + std::string(managedName));
        }

        // ---- Blocks the fake hands out, which Iris must give back with freeString.

        /** @brief A block holding `payload` behind the length prefix the bridge writes. Tracked until it is given back.
         */
        [[nodiscard]] std::uint64_t Block(const std::vector<std::uint8_t>& payload)
        {
            return BlockClaiming(static_cast<std::int64_t>(payload.size()), payload);
        }

        /** @brief A block whose length prefix says `claimed` bytes, whatever `payload` holds. */
        [[nodiscard]] std::uint64_t BlockClaiming(std::int64_t claimed, const std::vector<std::uint8_t>& payload)
        {
            auto* memory = static_cast<std::uint8_t*>(std::malloc(sizeof(claimed) + payload.size()));
            std::memcpy(memory, &claimed, sizeof(claimed));
            if (!payload.empty())
            {
                std::memcpy(memory + sizeof(claimed), payload.data(), payload.size());
            }
            const auto address = reinterpret_cast<std::uintptr_t>(memory);
            m_outstanding.insert(address);
            return address;
        }

        /** @brief How many blocks were handed out and not given back. */
        [[nodiscard]] std::size_t OutstandingBlocks() const noexcept
        {
            return m_outstanding.size();
        }

        /** @brief How many times Iris gave back something that was not an outstanding block (twice, or never handed
         * out). */
        [[nodiscard]] int BadFrees() const noexcept
        {
            return m_badFrees;
        }

        /** @brief The standard freeString: gives a tracked block back, and counts anything else as a bad free. */
        void FreeBlock(std::uint64_t block)
        {
            const auto found = m_outstanding.find(static_cast<std::uintptr_t>(block));
            if (found == m_outstanding.end())
            {
                ++m_badFrees;
                return;
            }
            std::free(reinterpret_cast<void*>(*found));
            m_outstanding.erase(found);
        }

        // ---- Answers for the queries the tests script most.

        /** @brief Makes QueryType answer Success with a block holding `payload`. */
        void AnswerQueryTypeWith(const std::vector<std::uint8_t>& payload)
        {
            queryType = [this, payload](std::uint64_t, std::uint64_t, std::int32_t, std::uint64_t* block, std::uint8_t*,
                                        std::int32_t, std::int32_t*, std::int32_t*) {
                *block = Block(payload);
                return 0;
            };
        }

        /** @brief Makes QueryType answer Success with a block whose length prefix says `claimed` bytes. */
        void AnswerQueryTypeWithLength(std::int64_t claimed, const std::vector<std::uint8_t>& payload)
        {
            queryType = [this, claimed, payload](std::uint64_t, std::uint64_t, std::int32_t, std::uint64_t* block,
                                                 std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*) {
                *block = BlockClaiming(claimed, payload);
                return 0;
            };
        }

        /** @brief Makes QueryType answer Success without a block. */
        void AnswerQueryTypeWithNoBlock()
        {
            queryType = [](std::uint64_t, std::uint64_t, std::int32_t, std::uint64_t* block, std::uint8_t*,
                           std::int32_t, std::int32_t*, std::int32_t*) {
                *block = 0;
                return 0;
            };
        }

      private:
        template <typename Signature>
        struct Defaults;
        template <typename R, typename... Arguments>
        struct Defaults<R(Arguments...)>
        {
            // Success (zero) and nothing written: the caller's outputs keep the values it gave them.
            static std::function<R(Arguments...)> Make()
            {
                if constexpr (std::is_void_v<R>)
                {
                    return [](Arguments...) {};
                }
                else
                {
                    return [](Arguments...) -> R { return R{}; };
                }
            }
        };

        std::shared_ptr<Table> m_tableStorage = std::make_shared<Table>();
        std::shared_ptr<const Table> m_table = m_tableStorage;
        std::array<int, Iris::Detail::BridgeFunctionNames.size()> m_calls{};
        std::set<std::uintptr_t> m_outstanding;
        int m_badFrees = 0;
    };

    namespace Detail
    {
        // The function in the table for one slot: it counts the call and passes it to the active fake's handler.
        template <typename Fn>
        struct Trampoline;
        template <typename R, typename... Arguments>
        struct Trampoline<R (*)(Arguments...)>
        {
            template <std::function<R(Arguments...)> FakeBridge::* Member, std::size_t Index>
            static R Call(Arguments... arguments)
            {
                FakeBridge* fake = ActiveFake();
                if (fake == nullptr)
                {
                    return R();
                }
                fake->NoteCall(Index);
                return (fake->*Member)(arguments...);
            }
        };
    } // namespace Detail

    // The table's slots are Cdecl (IRIS_BRIDGE_CALL), which is the default for a free function, so the trampolines
    // match them without saying so; a build that changes the default calling convention would not.
    inline FakeBridge::FakeBridge()
    {
        ActiveFake() = this;
#define IRIS_FAKE_INSTALL(field)                                                       \
    field = Defaults<std::remove_pointer_t<decltype(Table::field)>>::Make();           \
    m_tableStorage->field =                                                            \
        &Detail::Trampoline<decltype(Table::field)>::template Call<&FakeBridge::field, \
                                                                   offsetof(Table, field) / sizeof(void*)>;
        IRIS_FAKE_SLOTS(IRIS_FAKE_INSTALL)
#undef IRIS_FAKE_INSTALL

        // The few defaults that are not "success, nothing written".
        findType = [](std::uint64_t, const std::uint8_t*, std::int32_t, std::uint64_t* typeId, std::uint8_t*,
                      std::int32_t, std::int32_t*, std::int32_t*) {
            *typeId = 1;
            return 0;
        };
        freeString = [this](std::uint64_t block) { FreeBlock(block); };
    }

#define IRIS_FAKE_COUNT(field) +1
    static_assert(0 IRIS_FAKE_SLOTS(IRIS_FAKE_COUNT) == Iris::Detail::BridgeFunctionNames.size(),
                  "IRIS_FAKE_SLOTS must list every slot of the bridge function table");
#undef IRIS_FAKE_COUNT
#undef IRIS_FAKE_SLOTS
} // namespace Iris::Testing
