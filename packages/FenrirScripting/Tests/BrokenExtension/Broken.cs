// Iris would assign these fields when the host shares this assembly; the tests make sure it cannot.
#pragma warning disable CS0649

namespace Fenrir.BrokenExtension;

public static unsafe class Broken
{
    // No extension ever declares "Fenrir.BrokenExtension.Broken.Unregistered".
    public static delegate* unmanaged[Cdecl]<int> __native_Unregistered;

    // The tests declare "Fenrir.BrokenExtension.Broken.Mismatched" with a float parameter, not an int.
    public static delegate* unmanaged[Cdecl]<int, int> __native_Mismatched;
}
