// The numbers the native side gives to the things it shares with the managed bridge, compared with the table both sides
// read (numbering.txt). The managed side has the same check against the same table.
#include "BridgeFunctions.hpp"

#include "Iris/Iris.hpp"
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Iris;

    struct Item
    {
        const char* group;
        const char* name;
        std::int32_t number;
    };

#define IRIS_ITEM(group, scope, name) {#group, #name, static_cast<std::int32_t>(scope::name)}

    // Every item this side defines, written out. The table is what it is compared with.
    const std::vector<Item>& NativeItems()
    {
        using Kind = Detail::ValueKind;
        static const std::vector<Item> items = {
            IRIS_ITEM(ValueKind, Kind, Void),
            IRIS_ITEM(ValueKind, Kind, Bool),
            IRIS_ITEM(ValueKind, Kind, Int8),
            IRIS_ITEM(ValueKind, Kind, UInt8),
            IRIS_ITEM(ValueKind, Kind, Int16),
            IRIS_ITEM(ValueKind, Kind, UInt16),
            IRIS_ITEM(ValueKind, Kind, Int32),
            IRIS_ITEM(ValueKind, Kind, UInt32),
            IRIS_ITEM(ValueKind, Kind, Int64),
            IRIS_ITEM(ValueKind, Kind, UInt64),
            IRIS_ITEM(ValueKind, Kind, Float),
            IRIS_ITEM(ValueKind, Kind, Double),
            IRIS_ITEM(ValueKind, Kind, Object),
            IRIS_ITEM(ValueKind, Kind, String),
            IRIS_ITEM(ValueKind, Kind, Struct),
            IRIS_ITEM(ValueKind, Kind, Array),
            {"ValueKind", "StructArray", 16}, // Not a native enumerator: the wire combines Array with a struct handle.
            IRIS_ITEM(ValueKind, Kind, Pointer),
            IRIS_ITEM(ValueKind, Kind, Char),
            IRIS_ITEM(ValueKind, Kind, Enum),
            IRIS_ITEM(ValueKind, Kind, InlineArray),
            IRIS_ITEM(TypeKind, TypeKind, Other),
            IRIS_ITEM(TypeKind, TypeKind, Void),
            IRIS_ITEM(TypeKind, TypeKind, Bool),
            IRIS_ITEM(TypeKind, TypeKind, Char),
            IRIS_ITEM(TypeKind, TypeKind, Int8),
            IRIS_ITEM(TypeKind, TypeKind, UInt8),
            IRIS_ITEM(TypeKind, TypeKind, Int16),
            IRIS_ITEM(TypeKind, TypeKind, UInt16),
            IRIS_ITEM(TypeKind, TypeKind, Int32),
            IRIS_ITEM(TypeKind, TypeKind, UInt32),
            IRIS_ITEM(TypeKind, TypeKind, Int64),
            IRIS_ITEM(TypeKind, TypeKind, UInt64),
            IRIS_ITEM(TypeKind, TypeKind, Float),
            IRIS_ITEM(TypeKind, TypeKind, Double),
            IRIS_ITEM(TypeKind, TypeKind, String),
            IRIS_ITEM(TypeKind, TypeKind, Enum),
            IRIS_ITEM(TypeKind, TypeKind, Struct),
            IRIS_ITEM(TypeKind, TypeKind, Object),
            IRIS_ITEM(TypeKind, TypeKind, Array),
            IRIS_ITEM(Visibility, MemberVisibility, Private),
            IRIS_ITEM(Visibility, MemberVisibility, PrivateProtected),
            IRIS_ITEM(Visibility, MemberVisibility, Internal),
            IRIS_ITEM(Visibility, MemberVisibility, Protected),
            IRIS_ITEM(Visibility, MemberVisibility, ProtectedInternal),
            IRIS_ITEM(Visibility, MemberVisibility, Public),
            IRIS_ITEM(Status, CallStatus, Success),
            IRIS_ITEM(Status, CallStatus, InvalidArgument),
            IRIS_ITEM(Status, CallStatus, ManagedException),
            {"Status", "BufferTooSmall", Detail::WireBufferTooSmall},
            IRIS_ITEM(Status, CallStatus, InvalidHandle),
            IRIS_ITEM(Status, CallStatus, NotFound),
            IRIS_ITEM(Status, CallStatus, InUse),
            IRIS_ITEM(NativeStatus, CallStatus, NotInitialized),
            IRIS_ITEM(NativeStatus, CallStatus, UnexpectedStatus),
            IRIS_ITEM(NativeStatus, CallStatus, InvalidResponse),
            IRIS_ITEM(LogLevel, LogLevel, Debug),
            IRIS_ITEM(LogLevel, LogLevel, Info),
            IRIS_ITEM(LogLevel, LogLevel, Warning),
            IRIS_ITEM(LogLevel, LogLevel, Error),
            IRIS_ITEM(UnloadState, ComponentUnloadState, NotRequested),
            IRIS_ITEM(UnloadState, ComponentUnloadState, Pending),
            IRIS_ITEM(UnloadState, ComponentUnloadState, Collected),
        };
        return items;
    }

    // "group.name" to the number the table gives it.
    std::map<std::string, std::int32_t> Table()
    {
        std::ifstream file(IRIS_NUMBERING_FILE);
        INFO("The numbering table should be readable at " IRIS_NUMBERING_FILE);
        REQUIRE(file.is_open());
        std::map<std::string, std::int32_t> table;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::istringstream parts(line);
            std::string group;
            std::string name;
            std::int32_t number = 0;
            const bool parsed = static_cast<bool>(parts >> group >> name >> number);
            INFO("The numbering table has a line that is not \"<group> <name> <number>\": " << line);
            REQUIRE(parsed);
            INFO("The numbering table lists " << group << "." << name << " twice");
            REQUIRE(table.emplace(group + "." + name, number).second);
        }
        return table;
    }
} // namespace

TEST_CASE("every item the native side numbers has the number in the table", "[numbering]")
{
    const auto table = Table();
    for (const auto& item : NativeItems())
    {
        const std::string key = std::string(item.group) + "." + item.name;
        const auto found = table.find(key);
        INFO(key << " is not in the numbering table");
        REQUIRE(found != table.end());
        INFO(key << ": the table says " << found->second << " but the native side has " << item.number);
        REQUIRE(found->second == item.number);
    }
}

TEST_CASE("every item in the table is numbered by the native side", "[numbering]")
{
    std::map<std::string, std::int32_t> native;
    for (const auto& item : NativeItems())
    {
        native[std::string(item.group) + "." + item.name] = item.number;
    }
    for (const auto& [key, number] : Table())
    {
        INFO(key << " is in the numbering table (as " << number << ") but the native side does not define it");
        REQUIRE(native.count(key) == 1);
    }
}
