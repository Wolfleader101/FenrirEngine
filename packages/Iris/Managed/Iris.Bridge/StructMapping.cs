using System;
using System.Collections.Generic;
using System.Reflection;

namespace Iris.Bridge;

// The struct mapping a native host describes, and the check of it against the managed type. A struct is a first line
// "TypeName|size", then one line per field in declaration order, "name|offset|kind|nestedTypeName" (nestedTypeName
// is empty unless kind is Struct or Enum, and a nested type must be registered first); an inline array field has
// six parts, "name|offset|InlineArray|elementNestedTypeName|elementKind|length". An enum is one line,
// "TypeName|size|underlyingKind". Any mismatch throws BridgeArgumentException, naming it. The result is the
// StructRecord the codec uses. Nothing here knows about components: the nested types registered so far are given
// as IRegisteredStructs.
internal static class StructMapping
{
    // The structs and enums already registered for one component.
    internal interface IRegisteredStructs
    {
        bool Contains(Type type);
        StructRecord RecordOf(Type type);
    }

    // A description split into its header and field lines. Underlying is the enum header's third part, as written.
    internal sealed record Description(string TypeName, int Size, string? Underlying, string[] Lines)
    {
        internal bool IsEnum => Underlying != null;
    }

    internal static Description Parse(string description)
    {
        string[] lines = description.Split('\n');
        string[] header = lines[0].Split('|');
        bool isEnum = header.Length == 3;
        if ((header.Length != 2 && !isEnum) || (isEnum && lines.Length != 1) ||
            !int.TryParse(header[1], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int nativeSize))
            throw new BridgeArgumentException("The struct description is malformed.");
        return new Description(header[0], nativeSize, isEnum ? header[2] : null, lines);
    }

    // Checks the description against the managed type and returns what a copy needs to know about it.
    internal static StructRecord Describe(Type type, Description description, IRegisteredStructs registered)
    {
        if (!description.IsEnum) return ValidateStruct(registered, type, description.Size, description.Lines);
        if (!int.TryParse(description.Underlying, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int nativeUnderlying))
            throw new BridgeArgumentException("The enum description is malformed.");
        if (!type.IsEnum) throw new BridgeArgumentException($"{type} is not an enum.");
        Type underlyingType = Enum.GetUnderlyingType(type);
        int underlying = PrimitiveKindOf(underlyingType);
        if (underlying != nativeUnderlying)
            throw new BridgeArgumentException($"{type} is an enum of {underlyingType.Name}, which does not match the native enum's underlying type.");
        int enumSize = ManagedLayout.SizeOf(type);
        if (enumSize != description.Size)
            throw new BridgeArgumentException($"{type} is {enumSize} bytes but the native enum is {description.Size}.");
        return new StructRecord(type, enumSize, Array.Empty<int>(), underlying);
    }

    // The wire kind of a primitive, bool or char type, or -1.
    internal static int PrimitiveKindOf(Type type)
    {
        if (type == typeof(bool)) return (int)ValueKind.Bool;
        if (type == typeof(char)) return (int)ValueKind.Char;
        for (int kind = (int)ValueKind.Int8; kind <= (int)ValueKind.Double; ++kind)
            if (ValueKinds.ToType(kind, allowVoid: false) == type) return kind;
        return -1;
    }

    private static bool IsBlittableField(Type type) =>
        PrimitiveKindOf(type) >= 0 || type.IsEnum || (type.IsValueType && !type.IsPrimitive && BlittableProblem(type) == null);

    // Why a type cannot be copied byte for byte, or null if it can: a struct of sized numbers, bool, char,
    // enums, fixed-size arrays and other blittable structs, with sequential or explicit layout.
    internal static string? BlittableProblem(Type type)
    {
        if (!type.IsValueType || type.IsPrimitive || type.IsEnum || type.ContainsGenericParameters)
            return $"{type} is not a struct.";
        if (type.IsAutoLayout) return $"{type} has automatic layout, so its layout is not fixed.";
        FieldInfo[] fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        if (fields.Length == 0) return $"{type} has no fields.";
        foreach (FieldInfo field in fields)
            if (!IsBlittableField(field.FieldType))
                return $"{type} is not blittable: field '{field.Name}' has type {field.FieldType}.";
        return null;
    }

    // How a struct field looks on the wire. Nested is the field's struct or enum type; for an inline array,
    // ElementKind, ElementNested and Length describe the element.
    private readonly record struct FieldShape(int Kind, Type? Nested, int ElementKind, Type? ElementNested, int Length);

    private static int ScalarKindOf(Type type)
    {
        int primitive = PrimitiveKindOf(type);
        if (primitive >= 0) return primitive;
        return type.IsEnum ? (int)ValueKind.Enum : (int)ValueKind.Struct;
    }

    private static Type? NestedTypeOf(Type type) => type.IsEnum || (type.IsValueType && !type.IsPrimitive) ? type : null;

    // A fixed buffer (`fixed byte Name[16]`) or an [InlineArray] struct: its element type and length.
    private static bool TryGetInlineArray(FieldInfo field, out Type element, out int length)
    {
        element = typeof(void);
        length = 0;
        if (field.GetCustomAttribute<System.Runtime.CompilerServices.FixedBufferAttribute>() is { } fixedBuffer)
        {
            element = fixedBuffer.ElementType;
            length = fixedBuffer.Length;
            return true;
        }
        if (field.FieldType.GetCustomAttribute<System.Runtime.CompilerServices.InlineArrayAttribute>() is { } inline)
        {
            FieldInfo[] inner = field.FieldType.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (inner.Length != 1) return false;
            element = inner[0].FieldType;
            length = inline.Length;
            return true;
        }
        return false;
    }

    private static FieldShape ShapeOf(FieldInfo field)
    {
        if (TryGetInlineArray(field, out Type element, out int length))
            return new FieldShape((int)ValueKind.InlineArray, null, ScalarKindOf(element), NestedTypeOf(element), length);
        int kind = ScalarKindOf(field.FieldType);
        return new FieldShape(kind, kind is (int)ValueKind.Struct or (int)ValueKind.Enum ? field.FieldType : null, 0, null, 0);
    }

    private static string KindName(int kind) => kind switch
    {
        (int)ValueKind.Struct => "a struct",
        (int)ValueKind.Enum => "an enum",
        (int)ValueKind.InlineArray => "a fixed-size array",
        _ => ValueKinds.ToType(kind, allowVoid: false).Name
    };

    private static StructRecord ValidateStruct(IRegisteredStructs registered, Type type, int nativeSize, string[] lines)
    {
        if (BlittableProblem(type) is string problem) throw new BridgeArgumentException(problem);

        // Reflection does not promise declaration order; the metadata token does.
        FieldInfo[] fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        Array.Sort(fields, (left, right) => left.MetadataToken.CompareTo(right.MetadataToken));
        int managedSize = ManagedLayout.SizeOf(type);
        if (managedSize != nativeSize)
            throw new BridgeArgumentException($"{type} is {managedSize} bytes but the native struct is {nativeSize}.");
        if (lines.Length - 1 != fields.Length)
            throw new BridgeArgumentException($"{type} has {fields.Length} fields but the native struct declares {lines.Length - 1}.");
        var bools = new List<int>();
        for (int index = 0; index < fields.Length; ++index)
        {
            string[] parts = lines[index + 1].Split('|');
            if ((parts.Length != 4 && parts.Length != 6) || !int.TryParse(parts[1], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int offset) ||
                !int.TryParse(parts[2], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int kind))
                throw new BridgeArgumentException("The struct description is malformed.");
            FieldInfo field = fields[index];
            if (field.Name != parts[0])
                throw new BridgeArgumentException($"Field {index} of {type} is '{field.Name}' but the native struct has '{parts[0]}' there (field order must match).");
            int managedOffset = ManagedLayout.OffsetOf(field);
            if (managedOffset != offset)
                throw new BridgeArgumentException($"Field '{field.Name}' of {type} is at offset {managedOffset} but the native field is at {offset}.");
            FieldShape shape = ShapeOf(field);
            if ((ValueKind)kind == ValueKind.InlineArray)
            {
                if (parts.Length != 6 || !int.TryParse(parts[4], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int elementKind) ||
                    !int.TryParse(parts[5], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int length))
                    throw new BridgeArgumentException("The struct description is malformed.");
                if (shape.Kind != kind)
                    throw new BridgeArgumentException($"Field '{field.Name}' of {type} is {field.FieldType}, which does not match the native field's type (a fixed-size array).");
                if (shape.ElementKind != elementKind || !SameNested(registered, shape.ElementNested, elementKind, parts[3]))
                    throw new BridgeArgumentException($"The elements of field '{field.Name}' of {type} are {KindName(shape.ElementKind)}, which does not match the native array's element type.");
                if (shape.Length != length)
                    throw new BridgeArgumentException($"Field '{field.Name}' of {type} holds {shape.Length} elements but the native array holds {length}.");
                AddBoolOffsets(registered, bools, offset, shape.ElementKind, shape.ElementNested, length);
                continue;
            }
            if (parts.Length != 4 || shape.Kind != kind || !SameNested(registered, shape.Nested, kind, parts[3]))
            {
                string detail = shape.Nested != null && shape.Kind == kind
                    ? "which the native struct does not map to the same type"
                    : "which does not match the native field's type";
                throw new BridgeArgumentException($"Field '{field.Name}' of {type} is {field.FieldType}, {detail}.");
            }
            AddBoolOffsets(registered, bools, offset, kind, shape.Nested, 1);
        }
        return new StructRecord(type, managedSize, bools.ToArray());
    }

    // For a struct or enum kind: the type is already registered and has the native side's name. Any other
    // kind has no nested type.
    private static bool SameNested(IRegisteredStructs registered, Type? nested, int kind, string nativeName)
    {
        if (kind != (int)ValueKind.Struct && kind != (int)ValueKind.Enum) return nativeName.Length == 0;
        return nested != null && registered.Contains(nested) && nested.FullName == nativeName;
    }

    // Adds the bool byte offsets of `count` consecutive elements starting at `offset`.
    private static void AddBoolOffsets(IRegisteredStructs registered, List<int> bools, int offset, int kind, Type? nested, int count)
    {
        if (kind == (int)ValueKind.Bool)
        {
            for (int index = 0; index < count; ++index) bools.Add(offset + index);
        }
        else if (kind == (int)ValueKind.Struct)
        {
            StructRecord inner = registered.RecordOf(nested!);
            if (inner.BoolOffsets.Length == 0) return;
            for (int index = 0; index < count; ++index)
                foreach (int innerOffset in inner.BoolOffsets) bools.Add(offset + index * inner.Size + innerOffset);
        }
    }
}
