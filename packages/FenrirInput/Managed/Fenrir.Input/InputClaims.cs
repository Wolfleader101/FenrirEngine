namespace Fenrir.Input;

// The behaviour of InputClaims, read through Resources.GetReadOnly<InputClaims>(). Its storage (Entries, EntryCount,
// GamepadClaimed), WindowClaimsEntry, WindowInputClaims and CaretRect are generated from FenrirInputScripting's
// ScriptInputClaims and FenrirInput's InputRouter.hpp (Generated/).
public readonly partial struct InputClaims
{
    /// <summary>How many windows the claims can hold: C++'s ScriptInputClaims::MaxWindows, through the generated storage.</summary>
    public const int MaxWindows = EntriesArray.Length;

    /// <summary>Whether any router claims the gamepads, for UI navigation.</summary>
    public bool Gamepad => GamepadClaimed;

    /// <summary>How many windows any router claims something in.</summary>
    public int WindowCount => (int)System.Math.Min(EntryCount, (uint)MaxWindows);

    /// <summary>The window and claims at <paramref name="index"/>, from 0 to <see cref="WindowCount"/> less one.</summary>
    public WindowClaimsEntry this[int index]
    {
        get
        {
            if ((uint)index >= (uint)WindowCount)
                throw new ArgumentOutOfRangeException(nameof(index));
            return Entries[index];
        }
    }

    /// <summary>The claims raised in <paramref name="window"/>; none raised for a window no router claims anything in.</summary>
    public WindowInputClaims For(WindowId window)
    {
        int count = WindowCount;
        for (int index = 0; index < count; ++index)
        {
            if (Entries[index].Window == window)
                return Entries[index].Claims;
        }
        return default;
    }
}
