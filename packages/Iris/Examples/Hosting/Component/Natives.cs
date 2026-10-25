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
    // A native std::string_view parameter: UTF-8 bytes, then their length.
    public static delegate* unmanaged[Cdecl]<byte*, int, int> RecordText;
    public static delegate* unmanaged[Cdecl]<int, byte*, int, int, long> StringBetweenNumbers;
    public static delegate* unmanaged[Cdecl]<Vec3*, void> Normalize;
    public static delegate* unmanaged[Cdecl]<int> Throws;
    public static delegate* unmanaged[Cdecl]<int, void> Record;
    public static delegate* unmanaged[Cdecl]<bool, bool> Negate;
    public static delegate* unmanaged[Cdecl]<char, char> NextChar;
    public static delegate* unmanaged[Cdecl]<bool> LeakStopped;
    // A native enum parameter or result: its underlying integer (Gear's is an int, Shade's a byte).
    public static delegate* unmanaged[Cdecl]<int, int> NextGear;
    public static delegate* unmanaged[Cdecl]<byte, byte> Invert;
    // A native wrapper, a struct of one field, is that field: here a ticket's number, before a Gear.
    public static delegate* unmanaged[Cdecl]<uint, int, uint> Advance;

    public static int CallAdd(int left, int right) => Add(left, right);
    public static double CallHalf(double value) => Half(value);
    public static long CallCombine(long left, byte right) => Combine(left, right);
    public static int CallThrows() => Throws();
    public static void CallRecord(int value) => Record(value);
    public static bool CallNegate(bool value) => Negate(value);
    public static char CallNextChar(char value) => NextChar(value);
    public static bool CallLeakStopped() => LeakStopped();
    public static Gear CallNextGear(Gear gear) => (Gear)NextGear((int)gear);
    public static Shade CallInvert(Shade shade) => (Shade)Invert((byte)shade);
    public static uint CallAdvance(uint ticket, Gear by) => Advance(ticket, (int)by);

    // Reaches the shared contract's native function; only bound when the host shares the contract.
    public static int CallSharedTriple(int value) => Iris.Examples.Contracts.SharedNatives.CallTriple(value);

    public static int CallMeasure(string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* data = bytes) return Measure(data, bytes.Length);
    }

    public static int CallRecordText(string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* data = bytes) return RecordText(data, bytes.Length);
    }

    // Passes any address and length, valid or not, so the checks can see what the boundary refuses.
    public static int CallRecordTextRaw(long address, int length) => RecordText((byte*)address, length);

    public static long CallStringBetweenNumbers(int count, string label, int factor)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(label);
        fixed (byte* data = bytes) return StringBetweenNumbers(count, data, bytes.Length, factor);
    }

    public static Vec3 CallNormalize(Vec3 value)
    {
        Normalize(&value);
        return value;
    }
}
