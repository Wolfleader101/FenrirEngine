using System.Text;

namespace Iris.Examples.Component;

// Functions the host provides. Every static unmanaged function pointer field is bound by name
// ("Iris.Examples.Component.Natives.Add") when the component loads, and the load fails if the host has
// not registered a matching function. Strings cross as a UTF-8 pointer and a length, structs by pointer.
public static unsafe class Natives
{
    public static delegate* unmanaged[Cdecl]<int, int, int> Add;
    public static delegate* unmanaged[Cdecl]<double, double> Half;
    public static delegate* unmanaged[Cdecl]<long, byte, long> Combine;
    public static delegate* unmanaged[Cdecl]<byte*, int, int> Measure;
    public static delegate* unmanaged[Cdecl]<Vec3*, void> Normalize;
    public static delegate* unmanaged[Cdecl]<int> Throws;
    public static delegate* unmanaged[Cdecl]<int, void> Record;
    public static delegate* unmanaged[Cdecl]<bool, bool> Negate;
    public static delegate* unmanaged[Cdecl]<char, char> NextChar;

    public static int CallAdd(int left, int right) => Add(left, right);
    public static double CallHalf(double value) => Half(value);
    public static long CallCombine(long left, byte right) => Combine(left, right);
    public static int CallThrows() => Throws();
    public static void CallRecord(int value) => Record(value);
    public static bool CallNegate(bool value) => Negate(value);
    public static char CallNextChar(char value) => NextChar(value);

    public static int CallMeasure(string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* data = bytes) return Measure(data, bytes.Length);
    }

    public static Vec3 CallNormalize(Vec3 value)
    {
        Normalize(&value);
        return value;
    }
}
