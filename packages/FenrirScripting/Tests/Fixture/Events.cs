using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// An event only C# defines: no C++ type, no registration anywhere.
public struct Ping : IEvent
{
    public int Value;
}

// An event the test target registers from C++ under "Test.Alert", and reads and sends there too.
[NativeEvent("Test.Alert")]
public struct Alert : IEvent
{
    public int Code;
    public float Level;
}

// Claims the native event's name for a struct of its own.
[NativeEvent("Test.Alert")]
public struct FakeAlert : IEvent
{
    public double Code;
}

// Names an event nobody registered.
[NativeEvent("Test.NoSuchEvent")]
public struct Orphan : IEvent
{
    public int Value;
}

// What the event systems saw. They do nothing until a test enables them.
public static class EventProbe
{
    public static bool Enabled;

    public static readonly List<string> Events = new();

    public static void Enable() => Enabled = true;

    public static void Add(string text)
    {
        if (Enabled)
            Events.Add(text);
    }

    public static string Dump() => string.Join(",", Events);
}

// Order 40: sends a Ping on the first frame and reads Pings every frame. Its own send is not among them.
[System(Phase.Update, Order = 40)]
public sealed class PingSender : ISystem
{
    private int frame;

    public void OnUpdate(in SystemContext context)
    {
        if (!EventProbe.Enabled)
            return;
        frame++;
        if (frame == 1)
            context.Events.Send(new Ping { Value = 7 });
        EventProbe.Add("Own=" + context.Events.Read<Ping>().Count);
    }
}

// Order 41: runs after the sender returned, so it sees the send.
[System(Phase.Update, Order = 41)]
public sealed class PingReader : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (EventProbe.Enabled)
            EventProbe.Add("Later=" + context.Events.Read<Ping>().Count);
    }
}

// Order 42: reads the C++ event, which a native system sends on the first frame.
[System(Phase.Update, Order = 42)]
public sealed class AlertReader : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (EventProbe.Enabled)
            EventProbe.Add("Alerts=" + context.Events.Read<Alert>().Count);
    }
}

// Called by the tests through Iris, outside any system.
public static class EventScripts
{
    private static SystemContext Context => default;

    public static void SendPing(int value) => Context.Events.Send(new Ping { Value = value });

    public static void SendAlert(int code, float level) => Context.Events.Send(new Alert { Code = code, Level = level });

    public static string ReadAlerts()
    {
        var codes = new List<string>();
        foreach (var alert in Context.Events.Read<Alert>())
            codes.Add(alert.Code.ToString());
        return string.Join(",", codes);
    }

    public static int SumPings()
    {
        int sum = 0;
        foreach (var ping in Context.Events.Read<Ping>())
            sum += ping.Value;
        return sum;
    }

    // A send is not readable by the code that made it.
    public static int SendThenCount(int value)
    {
        Context.Events.Send(new Ping { Value = value });
        return Context.Events.Read<Ping>().Count;
    }

    // A view taken before sends is still the same events after them.
    public static bool ViewStaysValid()
    {
        var before = Context.Events.Read<Ping>();
        int count = before.Count;
        int first = before[0].Value;
        for (int index = 0; index < 100; index++)
            Context.Events.Send(new Ping { Value = 1000 + index });
        var after = Context.Events.Read<Ping>();
        return before.Count == count && after.Count == count && before[0].Value == first && after[0].Value == first;
    }

    public static string TryReadFake() => Message(() => { _ = Context.Events.Read<FakeAlert>().Count; });

    public static string TryReadOrphan() => Message(() => { _ = Context.Events.Read<Orphan>().Count; });

    private static string Message(Action action)
    {
        try
        {
            action();
            return "no exception";
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }
}
