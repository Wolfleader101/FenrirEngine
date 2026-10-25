using Fenrir.Scripting;
using Fenrir.TestExtension;

namespace Fenrir.TestExtensionScript;

// What the tests call, through Iris, to use the test extension from a script.
public static class Calls
{
    public static int Add(int left, int right) => Gadgets.Add(left, right);

    public static uint EchoEntity(uint entity) => Gadgets.EchoEntity(new Entity(entity)).Value;

    public static float HeightOf(uint entity) => Gadgets.HeightOf(new Entity(entity));

    public static void Lift(uint entity, float by) => Gadgets.Lift(new Entity(entity), by);

    // The Transform height as C# reads it from the engine's storage.
    public static float ScriptHeightOf(uint entity) => new Entity(entity).Get<Transform>().Value.Position.Y;

    public static int Label(string text, int number) => Gadgets.Label(text, number);

    // Text with a lone high surrogate, which has no UTF-8 form of its own: the generated body must send U+FFFD for it.
    public static int LabelWithLoneSurrogate(int number) => Gadgets.Label("a\uD800b", number);

    public static int Tally(int by) => Gadgets.Tally(by);

    public static byte TurnRight(byte heading) => (byte)Gadgets.TurnRight((Heading)heading);

    public static ushort Retag(ushort code, byte by) => Gadgets.Retag(new Tag { Code = code }, (Heading)by).Code;

    public static int ChargeLevel(uint entity) => new Entity(entity).Get<Charge>().Value.Level;

    // Native resources, read and written through Resources.
    public static float WeatherWind() => Resources.Get<Weather>().Wind;

    public static void SetWeather(float wind, int gusts)
    {
        var weather = Resources.Get<Weather>();
        weather.Wind = wind;
        weather.Gusts = gusts;
    }

    public static int SettingsVolume() => Resources.Get<Settings>().Volume;

    public static void SetVolume(int volume) => Resources.Get<Settings>().Volume = volume;

    public static double ClockSeconds() => Resources.GetReadOnly<Clock>().Seconds;

    public static bool HasWeather() => Resources.TryGet<Weather>(out _);

    // TryGet's view, read when it says there is one; -1 when it says there is none.
    public static float TryGetWind() => Resources.TryGet<Weather>(out var weather) ? weather.Wind : -1;

    public static bool HasClock() => Resources.TryGetReadOnly<Clock>(out _);

    public static bool MissingWeatherIsEmpty() => Resources.Get<Weather>().IsNull;

    // Records an insert, and says whether the resource is there yet (it must not be, until playback).
    public static bool InsertWeather(float wind)
    {
        Resources.Insert(new Weather { Wind = wind, Gusts = 2 });
        return Resources.TryGet<Weather>(out _);
    }

    public static void InsertDefaultSettings() => Resources.Insert<Settings>();

    // Records a removal, and says whether the resource is still there (it must be, until playback).
    public static bool RemoveWeather()
    {
        Resources.Remove<Weather>();
        return Resources.TryGet<Weather>(out _);
    }

    public static void RemoveThenInsertWeather(float wind)
    {
        Resources.Remove<Weather>();
        Resources.Insert(new Weather { Wind = wind });
    }

    public static void InsertThenRemoveSettings(int volume)
    {
        Resources.Insert(new Settings { Volume = volume });
        Resources.Remove<Settings>();
    }

    // A Ref taken before an insert and a removal is still good after them, until the call returns.
    public static float RefSurvivesInsertAndRemove(float wind)
    {
        var weather = Resources.Get<Weather>();
        Resources.Remove<Weather>();
        Resources.Insert(new Weather { Wind = 100 });
        weather.Wind = wind;
        return weather.Wind;
    }

    public static string? TryClaimClock() => Message(() => Resources.Get<ClockTwin>());

    public static string? TryAddWeatherAsComponent(uint entity) => Message(() => new Entity(entity).Add<Weather>());

    public static string? TryGetUnregistered() => Message(() => Resources.Get<Unregistered>());

    // The change buffer from an extension's native function: a created entity is usable at once.
    public static uint SpawnCharged(int level)
    {
        Entity entity = Gadgets.SpawnCharged(level);
        if (!entity.IsValid)
            throw new InvalidOperationException("The entity a native function created is not alive in the same call.");
        return entity.Value;
    }

    public static void Rearrange(uint kept, uint doomed, float wind) => Gadgets.Rearrange(new Entity(kept), new Entity(doomed), wind);

    public static uint SpawnUnder(uint parent) => Gadgets.SpawnUnder(new Entity(parent)).Value;

    private static string? Message(Action action)
    {
        try
        {
            action();
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
        return null;
    }
}

// A writable struct claiming the read-only Clock's name: the engine hands a resource only to the type it checked.
[NativeResource("Fenrir.TestExtension.Clock")]
public struct ClockTwin : IResource
{
    public double Seconds;
}

// A resource no extension registers.
[NativeResource("Fenrir.TestExtension.Unregistered")]
public struct Unregistered : IResource
{
    public int Value;
}
