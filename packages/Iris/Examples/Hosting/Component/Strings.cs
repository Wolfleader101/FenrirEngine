using System;

namespace Iris.Examples.Component;

// Strings in every position the string API has to handle: arguments, returns, null versus empty,
// text that is not ASCII, embedded nulls, and text with no UTF-8 form.
public static class Strings
{
    // Counts calls that reached managed code, so tests can show a rejected call never ran.
    public static int Calls;
    public static int Counted(string first, string second) => ++Calls;

    public static string Echo(string text) => text;
    public static string? EchoOrNull(string? text) => text;
    public static bool IsNull(string? text) => text is null;
    public static int Length(string text) => text.Length;
    public static string Join(string left, string right) => left + right;
    public static string Greeting() => "héllo π 🐺 — ok";
    public static string WithNull() => "A\0B";
    public static string Empty() => string.Empty;
    public static string? Null() => null;
    public static string Repeat(string text, int count) => CountedRepeat(text, count);
    private static string CountedRepeat(string text, int count)
    {
        ++Calls;
        return string.Concat(System.Linq.Enumerable.Repeat(text, count));
    }
    public static string UnpairedSurrogate() => "A\uD800B";
    public static string Throws(string text) => throw new InvalidOperationException("Rejected: " + text);

    // A string reaches overload resolution as its own kind, never as an object.
    public static int Kind(string text) => 1;
    public static int Kind(object? value) => 2;
}

public class Greeter
{
    public Greeter(string name) => Name = name;
    public string Name;
    public string Greet(string punctuation) => "Hello, " + Name + punctuation;
}
