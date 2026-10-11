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
    internal enum Query { Base = 0, Interfaces = 1, Attributes = 2 }

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
                IList<CustomAttributeData> attributes = CustomAttributeData.GetCustomAttributes(type);
                writer.Int32(attributes.Count);
                foreach (CustomAttributeData attribute in attributes)
                {
                    WriteType(writer, attribute.AttributeType);
                    writer.Int32(attribute.ConstructorArguments.Count + attribute.NamedArguments.Count);
                    foreach (CustomAttributeTypedArgument argument in attribute.ConstructorArguments) WriteArgument(writer, "", argument);
                    foreach (CustomAttributeNamedArgument named in attribute.NamedArguments) WriteArgument(writer, named.MemberName, named.TypedValue);
                }
                break;
            default:
                throw new BridgeArgumentException($"Unknown type query {(int)query}.");
        }
        return writer.ToArray();
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
