using System;
using System.Runtime.InteropServices;
using Iris.Examples.Dependency;

namespace Iris.Examples.Component;

public static class Calculations
{
    public static int Add(int left, int right) => DependencyMath.Add(left, right);
    public static int LocalAdd(int left, int right) => checked(left + right);
    public static long Add(long left, long right) => checked(left + right);
    public static int Throw(int left, int right) => throw new InvalidOperationException("Component failure \u2014 \u03C0 \U0001F43A.");
    public static int ThrowArgument(int left, int right) => throw new ArgumentException("An exception inside component code is not an ABI argument failure.");
    public static int LongError(int left, int right) => throw new InvalidOperationException(new string('\u03C0', 3000));

    // One method per supported value kind, for checking typed invocation and marshalling.
    public static double Scale(double value, float factor) => value * factor;
    public static bool Not(bool value) => !value;
    public static byte WrapByte(byte value) => unchecked((byte)(value + 1));
    public static sbyte NegateSByte(sbyte value) => unchecked((sbyte)-value);
    public static short NegateShort(short value) => unchecked((short)-value);
    public static ushort IncrementUShort(ushort value) => unchecked((ushort)(value + 1));
    public static uint IncrementUInt(uint value) => unchecked(value + 1);
    public static ulong Sum64(ulong left, ulong right) => unchecked(left + right);
    public static float Halve(float value) => value / 2;

    // Static state, to show that a void method really ran.
    private static int touches;
    public static void Touch() => ++touches;
    public static int Touches() => touches;

    // Wrong signatures and unmanaged-only entry points must never be invoked here.
    public static long WrongReturn(int left, int right) => left + (long)right;
    public static int WrongParameters(long left, long right) => throw new Exception("Wrong signature was invoked.");
    public static int Generic<T>(int left, int right) => left + right;
    public static int ByRef(ref int left, int right) => left + right;
    private static int Hidden(int left, int right) => left + right;
    [UnmanagedCallersOnly]
    public static int UnmanagedOnly(int left, int right) => left + right;
}

public class InstanceCalculation
{
    public int Add(int left, int right) => left + right;
}

public static class OpenCalculation<T>
{
    public static int Add(int left, int right) => left + right;
}

internal static class HiddenCalculation
{
    public static int Add(int left, int right) => left + right;
}
