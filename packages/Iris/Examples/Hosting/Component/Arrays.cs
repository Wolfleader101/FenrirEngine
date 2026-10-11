using System;
using System.Linq;

namespace Iris.Examples.Component;

// Arrays in every position the array API has to handle.
public static class Arrays
{
    public static int[] Range(int count) => Enumerable.Range(0, count).ToArray();
    public static int Sum(int[] values) => values.Sum();
    public static bool IsNull(int[]? values) => values is null;
    public static int[]? Null() => null;
    public static int[] Empty() => Array.Empty<int>();
    public static int[]? EchoOrNull(int[]? values) => values;
    public static int[] Fill(int[] values, int value)
    {
        Array.Fill(values, value);
        return values;
    }
    public static double[] Scale(double[] values, double factor) => values.Select(item => item * factor).ToArray();
    public static string[] Words() => new[] { "alpha", "π🐺", "", "A\0B" };
    public static string[] WordsWithNull() => new string?[] { "first", null, "third" }!;
    public static string Join(string[] words) => string.Join("+", words.Select(word => word ?? "<null>"));
    public static Vec3[] Line(int count) => Enumerable.Range(0, count).Select(index => new Vec3 { X = index, Y = index * 2, Z = index * 3 }).ToArray();
    public static float SumX(Vec3[] points) => points.Sum(point => point.X);
    public static bool[] Flags() => new[] { true, false, true };
    public static byte[] Bytes() => new byte[] { 1, 2, 255 };
    public static int Count(object[] items) => items.Length;
    public static int Jagged(int[][] rows) => rows.Length;

    private static readonly int[] Shared = { 1, 2, 3 };
    public static int[] SameArray() => Shared;
    public static int SharedFirst() => Shared[0];
}
