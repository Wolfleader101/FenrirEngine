#pragma once

// The trees of FenrirAssets' test package's data assets on both sides (ADR-0083 §3): C#'s, through the classes and
// codecs Iris.Bindings generates from DataAssetsTest.binding.toml, and C++'s, through the bridge. The binding's
// generated defaults test checks each data asset with CheckDataAssetDefaults ([tests] data-asset-check), and
// DataAssets.cpp round-trips trees through C#'s codecs with TreeBytes.

#include "DataAssetsTestPackage.hpp"
#include "Generated/DataAssetsTestPackageTypes.hpp"

#include "ExtensionTestApp.hpp"

#include <Iris/Component.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace Fenrir::Testing
{
    /** @brief A registry holding the test package's types, as a game's would. */
    inline const TypeRegistry& DataAssetTestTypes()
    {
        static const TypeRegistry registry = [] {
            TypeRegistry types;
            const bool registered = Generated::RegisterDataAssetsTestPackageTypes(types);
            INFO("The data asset test package's types did not register");
            REQUIRE(registered);
            return types;
        }();
        return registry;
    }

    /** @brief The bytes of a managed `byte[]`, null as none. */
    inline std::optional<std::vector<std::byte>> BytesOf(const std::optional<Iris::Array<std::uint8_t>>& array)
    {
        if (!array.has_value())
            return std::nullopt;
        const auto length = array->Length();
        INFO("The managed byte[]'s length could not be read: " << length.diagnostic);
        REQUIRE(length.Succeeded());
        std::vector<std::uint8_t> bytes(*length.value);
        INFO("The managed byte[]'s bytes could not be copied out");
        REQUIRE(array->CopyOut(0, std::span<std::uint8_t>(bytes)).Succeeded());
        std::vector<std::byte> result(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i)
            result[i] = static_cast<std::byte>(bytes[i]);
        return result;
    }

    /**
     * @brief Calls the static `byte[] method(Args...)` of `typeName` in the loaded C# assembly, requiring each step,
     * and gives the bytes it returned; none when it returned null.
     */
    template <typename... Args>
    std::optional<std::vector<std::byte>> CallForBytes(const Iris::Component* component, std::string_view typeName,
                                                       std::string_view method, const Args&... arguments)
    {
        {
            INFO("There is no loaded C# assembly to call");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(typeName);
        {
            INFO("The type " << typeName << " was not found: " << type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<std::optional<Iris::Array<std::uint8_t>>(Args...)>(method);
        {
            INFO("The method " << method << " was not found: " << handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const auto result = handle.value->Invoke(arguments...);
        {
            INFO("The method " << method << " failed: " << result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        return BytesOf(*result.value);
    }

    /** @brief The tree encoding of a C++ value through the bridge, fields at `T{}`'s left out, with no resolvers. */
    template <Described T>
    std::vector<std::byte> TreeBytes(const T& value)
    {
        return Serialisation::EncodeTree(
            Serialisation::ToTree(&value, Describe<T>::Description(), DataAssetTestTypes(), {}),
            Serialisation::Trivia::Drop);
    }

    /** @brief How many fields a file writes of the type: its base's first, then its own, none transient. */
    inline std::size_t WrittenFields(const TypeDescription& type)
    {
        std::size_t count = 0;
        if (!type.base.empty())
        {
            const TypeDescription* base = DataAssetTestTypes().Find(type.base);
            INFO("The base " << type.base << " of " << type.name << " is not registered");
            REQUIRE(base != nullptr);
            count += WrittenFields(*base);
        }
        for (const FieldInfo& field : type.fields)
            if (!field.transient)
                ++count;
        return count;
    }

    /**
     * @brief The data asset binding's defaults check: C#'s `new T()` through its generated codec gives C++'s `T{}`
     * through the bridge, byte for byte in the tree encoding. `method(bool full)` in `defaultsClass` gives C#'s tree,
     * every field when full. With defaults left out, C#'s bytes are C++'s; C#'s whole tree holds every field C++
     * writes, each in a form C++ reads with nothing defaulted, dropped or unresolved; and what C++ reads from it is
     * `T{}`.
     */
    template <Described T>
    void CheckDataAssetDefaults(const Iris::Component* component, std::string_view defaultsClass,
                                std::string_view method)
    {
        const TypeDescription& type = Describe<T>::Description();
        const std::vector<std::byte> native = TreeBytes(T{});

        const auto written = CallForBytes(component, defaultsClass, method, false);
        {
            INFO("C#'s tree of " << type.name << ", defaults left out, is null");
            REQUIRE(written.has_value());
        }
        {
            INFO("C#'s codec, leaving out the fields at their defaults, should write C++'s " << type.name << "{} tree");
            REQUIRE(*written == native);
        }

        const auto whole = CallForBytes(component, defaultsClass, method, true);
        {
            INFO("C#'s whole tree of " << type.name << " is null");
            REQUIRE(whole.has_value());
        }
        RecordingLogger logger;
        const Serialisation::ReadTreeResult decoded = Serialisation::DecodeTree(*whole, "C#'s new T()", logger);
        {
            INFO("C#'s whole tree of " << type.name << " is not one object in the tree encoding");
            REQUIRE(decoded.tree.has_value());
            REQUIRE(decoded.tree->Kind() == Serialisation::ValueKind::Object);
        }
        {
            INFO("C#'s whole tree of " << type.name << " should hold every field C++ writes, and no other");
            REQUIRE(decoded.tree->AsObject().members.size() == WrittenFields(type));
        }

        T read{};
        const Serialisation::BridgeResult result =
            Serialisation::FromTree(*decoded.tree, type, DataAssetTestTypes(), {}, &read, "C#'s new T()", logger);
        {
            INFO("C++ should read every field of C#'s whole tree of " << type.name
                                                                      << ", with nothing defaulted or dropped");
            REQUIRE(result.status == Serialisation::BridgeStatus::Succeeded);
        }
        {
            INFO("Every field of C#'s new T() should be C++'s " << type.name << "{}");
            REQUIRE(TreeBytes(read) == native);
        }
    }
} // namespace Fenrir::Testing
