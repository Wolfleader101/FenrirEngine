#pragma once

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir
{
    /**
     * @brief What walks an instance's fields without its C++ type in hand: the file writer, the file reader, an
     * inspector and C# bridging alike (ADR-0074).
     *
     * `TypeDescription::Visit` pushes each field to `Field` once, in storage order, whether the description walks a
     * table of offsets or a generated function; a visitor cannot tell which.
     */
    class FieldVisitor
    {
      public:
        virtual ~FieldVisitor() = default;

        /** @brief Called once per field with the field's description and a mutable reference to its value. */
        virtual void Field(const FieldInfo& field, ValueRef value) = 0;
    };

    /** @brief A field visitor that only reads: `TypeDescription::Visit` over a const instance pushes to one. */
    class ConstFieldVisitor
    {
      public:
        virtual ~ConstFieldVisitor() = default;

        /** @brief Called once per field with the field's description and a read-only reference to its value. */
        virtual void Field(const FieldInfo& field, ConstValueRef value) = 0;
    };
} // namespace Fenrir
