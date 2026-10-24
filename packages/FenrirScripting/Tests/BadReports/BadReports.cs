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

// The tables the SDK's generator would write, written by hand so a test can make them report something wrong end to end
// (every other refusal is checked without an assembly, in the rules tests). The mode is read as the assembly loads from
// the file the FENRIR_BAD_REPORT_FILE environment variable names (a file, because the test changes it after the runtime
// has started, and on Linux and macOS .NET does not see environment changes made after it starts):
//   (empty)                  one component and one event, reported once each: a sound script assembly
//   duplicate-component      the component twice
//   duplicate-event          the event twice
namespace Fenrir.Scripting.Generated
{
    internal static unsafe class Reports
    {
        public static string Mode
        {
            get
            {
                string? file = Environment.GetEnvironmentVariable("FENRIR_BAD_REPORT_FILE");
                return file != null && System.IO.File.Exists(file) ? System.IO.File.ReadAllText(file).Trim() : "";
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
            }
        }
    }
}
