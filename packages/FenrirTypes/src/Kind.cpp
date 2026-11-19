#include "FenrirTypes/Kind.hpp"

namespace Fenrir
{
    std::string_view KindName(Kind kind) noexcept
    {
        switch (kind)
        {
        case Kind::I8:
            return "i8";
        case Kind::I16:
            return "i16";
        case Kind::I32:
            return "i32";
        case Kind::I64:
            return "i64";
        case Kind::U8:
            return "u8";
        case Kind::U16:
            return "u16";
        case Kind::U32:
            return "u32";
        case Kind::U64:
            return "u64";
        case Kind::F32:
            return "f32";
        case Kind::F64:
            return "f64";
        case Kind::Bool:
            return "bool";
        case Kind::Char16:
            return "char16";
        case Kind::Enum:
            return "enum";
        case Kind::Struct:
            return "struct";
        case Kind::Array:
            return "array";
        case Kind::Pointer:
            return "pointer";
        case Kind::String:
            return "string";
        case Kind::List:
            return "list";
        case Kind::Optional:
            return "optional";
        case Kind::Reference:
            return "reference";
        case Kind::Entity:
            return "entity";
        case Kind::FixedString:
            return "fixed-string";
        case Kind::Path:
            return "path";
        }
        return "unknown";
    }

    std::string_view RoleName(Role role) noexcept
    {
        switch (role)
        {
        case Role::Component:
            return "component";
        case Role::Event:
            return "event";
        case Role::SceneResource:
            return "scene-resource";
        case Role::AppResource:
            return "app-resource";
        case Role::SharedStruct:
            return "shared-struct";
        case Role::BoundType:
            return "bound-type";
        case Role::Nested:
            return "nested";
        case Role::Enum:
            return "enum";
        case Role::DataAsset:
            return "data-asset";
        case Role::Settings:
            return "settings";
        }
        return "unknown";
    }
} // namespace Fenrir
