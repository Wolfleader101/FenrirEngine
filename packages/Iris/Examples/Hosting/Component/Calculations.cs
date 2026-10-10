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
