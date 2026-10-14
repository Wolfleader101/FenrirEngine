namespace Fenrir.Scripting;

/// <summary>
/// Marks a <c>static partial</c> method as a native function: a C++ function, declared by a scripting extension under this
/// method's full name (<c>Namespace.Type.Method</c>, with <c>+</c> for a nested type), that this method calls. The source
/// generator writes the method's body and the function pointer field the host binds (<c>__native_Method</c>).
/// </summary>
/// <remarks>
/// Parameters and the result may be sized integers, <c>float</c>, <c>double</c>, <c>bool</c>, <c>char</c>, pointers and
/// <see cref="Entity"/> (which crosses as its value). A <c>string</c> or <c>ReadOnlySpan&lt;char&gt;</c> parameter crosses as
/// UTF-8 bytes and their length (the C++ side takes a <c>std::string_view</c>), and an <c>in</c>, <c>ref</c> or <c>out</c>
/// parameter of an unmanaged struct as a pointer to it. A string is never a result. A <c>bool</c> or <c>char</c> needs
/// <c>[assembly: DisableRuntimeMarshalling]</c>, and the project needs <c>AllowUnsafeBlocks</c>. The method and every type
/// containing it must be partial and not generic, and a type cannot have two native functions of one name.
/// </remarks>
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false, Inherited = false)]
public sealed class NativeFunctionAttribute : Attribute
{
}
