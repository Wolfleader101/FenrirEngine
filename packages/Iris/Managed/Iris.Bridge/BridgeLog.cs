using System;
using System.Text;
using System.Threading;

namespace Iris.Bridge;

// Wire values, mirrored by the native LogLevel.
internal enum LogLevel : int
{
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
}

// The process-wide log sink: a native callback and its context, set through the bridge. Logging must
// never fail or slow a call, so nothing here throws. The native side keeps the callback's context alive
// for the life of the process, so a call racing with a clear is harmless.
internal static unsafe class BridgeLog
{
    private sealed class Sink(IntPtr callback, IntPtr context)
    {
        internal readonly IntPtr Callback = callback;
        internal readonly IntPtr Context = context;
    }

    private static Sink? current;

    // Set while this thread is inside the sink. A sink that makes another failing call would log
    // again and recurse without bound, so messages raised from inside the sink are dropped.
    [ThreadStatic] private static bool delivering;

    internal static void Set(IntPtr callback, IntPtr context) =>
        Volatile.Write(ref current, callback == IntPtr.Zero ? null : new Sink(callback, context));

    internal static bool IsEnabled => Volatile.Read(ref current) != null;

    internal static void Write(LogLevel level, string text)
    {
        Sink? sink = Volatile.Read(ref current);
        if (sink == null || delivering) return;
        delivering = true;
        try
        {
            byte[] bytes = Encoding.UTF8.GetBytes(text);
            fixed (byte* pointer = bytes)
                ((delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, void>)sink.Callback)(sink.Context, (int)level, pointer, bytes.Length);
        }
        catch (Exception)
        {
            // A sink problem never changes the outcome of the call being logged.
        }
        finally
        {
            delivering = false;
        }
    }

    // Reports an exception caught at a bridge boundary. The bridge's own validation is a Warning;
    // anything else, including every exception from component code, is an Error.
    internal static void Report(Exception exception)
    {
        if (!IsEnabled) return;
        string text;
        try { text = exception.ToString(); }
        catch (Exception) { text = exception.GetType().FullName ?? "An exception whose details could not be formatted."; }
        Write(exception is BridgeArgumentException or DecoderFallbackException ? LogLevel.Warning : LogLevel.Error, text);
    }
}
