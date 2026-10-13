using System.Reflection;
using System.Text;
using Fenrir.Scripting;

namespace Fenrir.BadReportFixture
{
    public struct Alpha : IComponent
    {
        public int Value;
    }

    public struct Beep : IEvent
    {
        public int Value;
    }
}

// The tables the SDK's generator would write, written by hand so a test can make them report something wrong. The mode comes
// from the FENRIR_BAD_REPORT environment variable, read as the assembly loads:
//   (empty)                  one component and one event, reported once each: a sound script assembly
//   duplicate-component      the component twice
//   bad-component-layout     a second component whose alignment (3) no C# struct has
//   duplicate-event          the event twice
//   bad-event-layout         a second event whose alignment (3) no C# struct has
namespace Fenrir.Scripting.Generated
{
    internal static unsafe class Reports
    {
        public static string Mode => Environment.GetEnvironmentVariable("FENRIR_BAD_REPORT") ?? "";

        // Reports a type with a size and alignment of the caller's choosing, straight to the engine's native function.
        public static void Raw(string field, string name, int size, int alignment)
        {
            var native = typeof(ScriptComponents).Assembly.GetType("Fenrir.Scripting.Native")!;
            var function = (delegate* unmanaged[Cdecl]<byte*, int, int, int, byte>)(nint)(IntPtr)native
                .GetField(field, BindingFlags.Public | BindingFlags.Static)!.GetValue(null)!;
            byte[] utf8 = Encoding.UTF8.GetBytes(name);
            fixed (byte* bytes = utf8)
            {
                if (function(bytes, utf8.Length, size, alignment) == 0)
                    throw new InvalidOperationException($"The engine refused {name} outright.");
            }
        }
    }

    public static class ComponentTable
    {
        public static void Register()
        {
            ScriptComponents.Report<Fenrir.BadReportFixture.Alpha>();
            switch (Reports.Mode)
            {
                case "duplicate-component":
                    ScriptComponents.Report<Fenrir.BadReportFixture.Alpha>();
                    break;
                case "bad-component-layout":
                    Reports.Raw("RegisterScriptComponent", "Fenrir.BadReportFixture.Odd", 6, 3);
                    break;
            }
        }
    }

    public static class EventTable
    {
        public static void Register()
        {
            ScriptEvents.Report<Fenrir.BadReportFixture.Beep>();
            switch (Reports.Mode)
            {
                case "duplicate-event":
                    ScriptEvents.Report<Fenrir.BadReportFixture.Beep>();
                    break;
                case "bad-event-layout":
                    Reports.Raw("RegisterScriptEvent", "Fenrir.BadReportFixture.OddEvent", 6, 3);
                    break;
            }
        }
    }
}
