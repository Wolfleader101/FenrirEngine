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

// Order 40: sends a Ping on the first frame and reads Pings every frame through its own reader. Its own send reaches it
// the frame after, once.
[System(Phase.Update, Order = 40)]
public sealed class PingSender : ISystem
{
    private readonly EventReader<Ping> pings = new();
    private int frame;

    public void OnUpdate(in SystemContext context)
    {
        if (!EventProbe.Enabled)
            return;
        frame++;
        if (frame == 1)
            context.Events.Send(new Ping { Value = 7 });
        EventProbe.Add("Own=" + pings.Read().Count);
    }
}

// Order 41: runs after the sender returned, so it sees the send in the same frame.
[System(Phase.Update, Order = 41)]
public sealed class PingReader : ISystem
{
    private readonly EventReader<Ping> pings = new();

    public void OnUpdate(in SystemContext context)
    {
        if (EventProbe.Enabled)
            EventProbe.Add("Later=" + pings.Read().Count);
    }
}

// Order 42: reads the C++ event, which native systems send.
[System(Phase.Update, Order = 42)]
public sealed class AlertReader : ISystem
{
    private readonly EventReader<Alert> alerts = new();

    public void OnUpdate(in SystemContext context)
    {
        if (EventProbe.Enabled)
            EventProbe.Add("Alerts=" + alerts.Read().Count);
    }
}

// Reads the C++ event in the fixed step, which runs zero, one or several times a frame.
[System(Phase.Tick, Order = 42)]
public sealed class TickAlertReader : ISystem
{
    private readonly EventReader<Alert> alerts = new();

    public void OnUpdate(in SystemContext context)
    {
        if (EventProbe.Enabled)
            EventProbe.Add("Tick=" + alerts.Read().Count);
    }
}

// Called by the tests through Iris, outside any system. Each helper has a reader of its own, as a system would.
public static class EventScripts
{
    private static readonly EventReader<Alert> alerts = new();
    private static readonly EventReader<Ping> summed = new();
    private static readonly EventReader<Ping> counted = new();
    private static readonly EventReader<Ping> viewed = new();

    private static SystemContext Context => default;

    public static void SendPing(int value) => Context.Events.Send(new Ping { Value = value });

    public static void SendAlert(int code, float level) => Context.Events.Send(new Alert { Code = code, Level = level });

    public static string ReadAlerts()
    {
        var codes = new List<string>();
        foreach (var alert in alerts.Read())
            codes.Add(alert.Code.ToString());
        return string.Join(",", codes);
    }

    public static int SumPings()
    {
        int sum = 0;
        foreach (var ping in summed.Read())
            sum += ping.Value;
        return sum;
    }

    // A send is not readable by the code that made it until it returns.
    public static int SendThenCount(int value)
    {
        Context.Events.Send(new Ping { Value = value });
        return counted.Read().Count;
    }

    // What a read yielded is still the same events after the reader sends more, and those sends are not read yet.
    public static bool ViewStaysValid()
    {
        var before = viewed.Read();
        int count = before.Count;
        int first = before[0].Value;
        for (int index = 0; index < 100; index++)
            Context.Events.Send(new Ping { Value = 1000 + index });
        var after = viewed.Read();
        return count > 0 && before.Count == count && before[0].Value == first && after.Count == 0;
    }

    public static string TryReadFake() => Message(() => { _ = new EventReader<FakeAlert>().Read().Count; });

    public static string TryReadOrphan() => Message(() => { _ = new EventReader<Orphan>().Read().Count; });

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
