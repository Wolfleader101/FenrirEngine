#pragma once

#include "FenrirTypes/FieldInfo.hpp"

namespace Fenrir::Serialisation
{
    /**
     * @brief Calls `call` with the field a resolver is handed for a value of type `type` held by `field`: the field
     * itself when the value is the field's own, or for an element of an array, list or optional, a copy of the field
     * whose `type` is the element's. Either way the field handed names the value's target type in `type.target`.
     */
    template <typename Call>
    decltype(auto) HandedField(const FieldInfo& field, const ValueType& type, Call&& call)
    {
        if (&field.type == &type)
            return call(field);
        FieldInfo element = field;
        element.type = type;
        return call(static_cast<const FieldInfo&>(element));
    }
} // namespace Fenrir::Serialisation
