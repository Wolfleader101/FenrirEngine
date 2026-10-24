using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Iris.Bridge;

// Snapshots of what reflection says about a type, serialized for the native side: base type, interfaces and
// custom attributes. Nothing here runs component code: attributes are read as metadata (CustomAttributeData),
// never instantiated. A snapshot is plain data, so it holds no reference to the component.
//
// Format (little-endian): int32 and int64 as themselves, a double as its 64 raw bits, a string as an int32
// byte length and that many UTF-8 bytes (lone surrogates become U+FFFD).
internal static class TypeQueries
{
    internal enum Query { Base = 0, Interfaces = 1, Attributes = 2, Fields = 3, Properties = 4, Methods = 5 }

    // What kind of managed type a member has; mirrored by the native TypeKind.
    private enum Kind { Other = 0, Void = 1, Bool = 2, Char = 3, Int8 = 4, UInt8 = 5, Int16 = 6, UInt16 = 7, Int32 = 8, UInt32 = 9, Int64 = 10, UInt64 = 11, Float = 12, Double = 13, String = 14, Enum = 15, Struct = 16, Object = 17, Array = 18 }

    // Accessibility; mirrored by the native MemberVisibility.
    private enum Visibility { Private = 0, PrivateProtected = 1, Internal = 2, Protected = 3, ProtectedInternal = 4, Public = 5 }

    private enum Tag { Null = 0, Bool = 1, Signed = 2, Unsigned = 3, Real = 4, Text = 5, Unsupported = 6 }

    private sealed class Writer
    {
        private readonly MemoryStream stream = new();
        internal void Int32(int value) => stream.Write(BitConverter.GetBytes(value));
        internal void Int64(long value) => stream.Write(BitConverter.GetBytes(value));
        internal void Double(double value) => Int64(BitConverter.DoubleToInt64Bits(value));
        internal void String(string? value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value ?? "");
            Int32(bytes.Length);
            stream.Write(bytes);
        }
        internal byte[] ToArray() => stream.ToArray();
    }

    private static string NameOf(Type type) => type.FullName ?? type.Name;

    private static void WriteType(Writer writer, Type type)
    {
        writer.String(NameOf(type));
        writer.String(type.Assembly.GetName().Name);
    }

    internal static byte[] Run(Type type, Query query)
    {
        var writer = new Writer();
        switch (query)
        {
            case Query.Base:
                if (type.BaseType is Type baseType)
                {
                    writer.Int32(1);
                    WriteType(writer, baseType);
                }
                else
                {
                    writer.Int32(0);
                }
                break;
            case Query.Interfaces:
                // Every interface the type implements, including inherited ones, sorted by name so the order is stable.
                Type[] interfaces = type.GetInterfaces().OrderBy(NameOf, StringComparer.Ordinal).ToArray();
                writer.Int32(interfaces.Length);
                foreach (Type implemented in interfaces) WriteType(writer, implemented);
                break;
            case Query.Attributes:
                WriteAttributes(writer, type);
                break;
            case Query.Fields:
                WriteFields(writer, type);
                break;
            case Query.Properties:
                WriteProperties(writer, type);
                break;
            case Query.Methods:
                WriteMethods(writer, type);
                break;
            default:
                throw new BridgeArgumentException($"Unknown type query {(int)query}.");
        }
        return writer.ToArray();
    }

    private static void WriteAttributes(Writer writer, MemberInfo member)
    {
        IList<CustomAttributeData> attributes = CustomAttributeData.GetCustomAttributes(member);
        writer.Int32(attributes.Count);
        foreach (CustomAttributeData attribute in attributes)
        {
            WriteType(writer, attribute.AttributeType);
            writer.Int32(attribute.ConstructorArguments.Count + attribute.NamedArguments.Count);
            foreach (CustomAttributeTypedArgument argument in attribute.ConstructorArguments) WriteArgument(writer, "", argument);
            foreach (CustomAttributeNamedArgument named in attribute.NamedArguments) WriteArgument(writer, named.MemberName, named.TypedValue);
        }
    }

    // Members declared on the type itself (inherited ones belong to the base type), public or not, in
    // declaration order (the metadata token; reflection does not promise it). Compiler-generated members
    // are left out.
    private const BindingFlags Declared = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    private static bool IsGenerated(MemberInfo member) => member.IsDefined(typeof(System.Runtime.CompilerServices.CompilerGeneratedAttribute), inherit: false);

    private static Kind KindOf(Type type)
    {
        if (type == typeof(void)) return Kind.Void;
        if (type == typeof(bool)) return Kind.Bool;
        if (type == typeof(char)) return Kind.Char;
        if (type == typeof(sbyte)) return Kind.Int8;
        if (type == typeof(byte)) return Kind.UInt8;
        if (type == typeof(short)) return Kind.Int16;
        if (type == typeof(ushort)) return Kind.UInt16;
        if (type == typeof(int)) return Kind.Int32;
        if (type == typeof(uint)) return Kind.UInt32;
        if (type == typeof(long)) return Kind.Int64;
        if (type == typeof(ulong)) return Kind.UInt64;
        if (type == typeof(float)) return Kind.Float;
        if (type == typeof(double)) return Kind.Double;
        if (type == typeof(string)) return Kind.String;
        if (type.ContainsGenericParameters || type.IsByRef || type.IsPointer) return Kind.Other;
        if (type.IsEnum) return Kind.Enum;
        if (type.IsArray) return Kind.Array;
        if (type.IsValueType) return Kind.Struct;
        return type.IsClass || type.IsInterface ? Kind.Object : Kind.Other;
    }

    private static Visibility VisibilityOf(MethodAttributes attributes) => (attributes & MethodAttributes.MemberAccessMask) switch
    {
        MethodAttributes.Public => Visibility.Public,
        MethodAttributes.FamORAssem => Visibility.ProtectedInternal,
        MethodAttributes.Family => Visibility.Protected,
        MethodAttributes.Assembly => Visibility.Internal,
        MethodAttributes.FamANDAssem => Visibility.PrivateProtected,
        _ => Visibility.Private
    };

    private static Visibility VisibilityOf(FieldAttributes attributes) => (attributes & FieldAttributes.FieldAccessMask) switch
    {
        FieldAttributes.Public => Visibility.Public,
        FieldAttributes.FamORAssem => Visibility.ProtectedInternal,
        FieldAttributes.Family => Visibility.Protected,
        FieldAttributes.Assembly => Visibility.Internal,
        FieldAttributes.FamANDAssem => Visibility.PrivateProtected,
        _ => Visibility.Private
    };

    private static void WriteFields(Writer writer, Type type)
    {
        FieldInfo[] fields = type.GetFields(Declared).Where(field => !IsGenerated(field) && !field.IsSpecialName).ToArray();
        Array.Sort(fields, (left, right) => left.MetadataToken.CompareTo(right.MetadataToken));
        // An instance field of a blittable struct has a byte offset, the same one struct-mapping validation checks.
        bool hasOffsets = type.IsValueType && !type.IsEnum && StructMapping.BlittableProblem(type) == null;
        writer.Int32(fields.Length);
        foreach (FieldInfo field in fields)
        {
            writer.String(field.Name);
            WriteType(writer, field.FieldType);
            writer.Int32((int)KindOf(field.FieldType));
            writer.Int32((field.IsStatic ? 1 : 0) | (field.IsInitOnly || field.IsLiteral ? 2 : 0));
            writer.Int32((int)VisibilityOf(field.Attributes));
            writer.Int64(hasOffsets && !field.IsStatic ? ManagedLayout.OffsetOf(field) : -1);
            WriteAttributes(writer, field);
        }
    }

    private static void WriteProperties(Writer writer, Type type)
    {
        PropertyInfo[] properties = type.GetProperties(Declared).Where(property => !IsGenerated(property) && property.GetIndexParameters().Length == 0).ToArray();
        Array.Sort(properties, (left, right) => left.MetadataToken.CompareTo(right.MetadataToken));
        writer.Int32(properties.Length);
        foreach (PropertyInfo property in properties)
        {
            MethodInfo? getter = property.GetGetMethod(nonPublic: true), setter = property.GetSetMethod(nonPublic: true);
            MethodInfo accessor = (getter ?? setter)!;
            // The more accessible accessor decides.
            Visibility visibility = VisibilityOf(accessor.Attributes);
            if (getter != null && setter != null && VisibilityOf(setter.Attributes) > visibility) visibility = VisibilityOf(setter.Attributes);
            writer.String(property.Name);
            WriteType(writer, property.PropertyType);
            writer.Int32((int)KindOf(property.PropertyType));
            writer.Int32((accessor.IsStatic ? 1 : 0) | (getter != null ? 2 : 0) | (setter != null ? 4 : 0));
            writer.Int32((int)visibility);
            WriteAttributes(writer, property);
        }
    }

    private static void WriteMethods(Writer writer, Type type)
    {
        MethodInfo[] methods = type.GetMethods(Declared).Where(method => !IsGenerated(method) && !method.IsSpecialName).ToArray();
        Array.Sort(methods, (left, right) => left.MetadataToken.CompareTo(right.MetadataToken));
        writer.Int32(methods.Length);
        foreach (MethodInfo method in methods)
        {
            writer.String(method.Name);
            WriteType(writer, method.ReturnType);
            writer.Int32((int)KindOf(method.ReturnType));
            writer.Int32(method.IsStatic ? 1 : 0);
            writer.Int32((int)VisibilityOf(method.Attributes));
            ParameterInfo[] parameters = method.GetParameters();
            writer.Int32(parameters.Length);
            foreach (ParameterInfo parameter in parameters)
            {
                writer.String(parameter.Name);
                WriteType(writer, parameter.ParameterType);
                writer.Int32((int)KindOf(parameter.ParameterType));
            }
            WriteAttributes(writer, method);
        }
    }

    // name is empty for a constructor argument. Anything that is not a bool, number, string or Type is reported
    // as Unsupported with its type name. Reading attributes loads the assemblies of the attribute types and of
    // any typeof() argument; one that cannot be loaded fails the whole query.
    private static void WriteArgument(Writer writer, string name, CustomAttributeTypedArgument argument)
    {
        writer.String(name);
        writer.String(NameOf(argument.ArgumentType));
        object? value = argument.Value;
        switch (value)
        {
            case null:
                writer.Int32((int)Tag.Null);
                break;
            case bool flag:
                writer.Int32((int)Tag.Bool);
                writer.Int64(flag ? 1 : 0);
                break;
            case sbyte or short or int or long:
                writer.Int32((int)Tag.Signed);
                writer.Int64(Convert.ToInt64(value));
                break;
            case byte or ushort or uint or ulong or char:
                writer.Int32((int)Tag.Unsigned);
                writer.Int64(unchecked((long)(value is char letter ? letter : Convert.ToUInt64(value))));
                break;
            case float or double:
                writer.Int32((int)Tag.Real);
                writer.Double(Convert.ToDouble(value));
                break;
            case string text:
                writer.Int32((int)Tag.Text);
                writer.String(text);
                break;
            case Type type:
                writer.Int32((int)Tag.Text);
                writer.String(NameOf(type));
                break;
            default:
                // Arrays and anything else: not representable.
                writer.Int32((int)Tag.Unsupported);
                break;
        }
    }

    // A block the caller copies and gives back with FreeString: an eight-byte length, then the bytes.
    internal static unsafe ulong ToBlock(byte[] data)
    {
        byte* block = (byte*)NativeMemory.Alloc((nuint)data.Length + 8);
        *(long*)block = data.Length;
        data.AsSpan().CopyTo(new Span<byte>(block + 8, data.Length));
        return (ulong)block;
    }
}
