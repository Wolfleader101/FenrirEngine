using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

public enum PickupType : byte
{
    Coin,
    Gem,
    TreasureChest,
}

public struct Coin
{
    public int Value;
}

public struct Gem
{
    public float Shine;
    public int Colour;
}

public struct TreasureChest
{
    public long Contents;
}

// A byte tag with a noun: every generated member must exist and be usable like the hand-written ones.
[TaggedUnion(Noun = "pickup")]
[StructLayout(LayoutKind.Explicit)]
public partial struct Pickup
{
    [FieldOffset(0)] private PickupType _type;
    [FieldOffset(8)] private Coin _coin;
    [FieldOffset(8)] private Gem _gem;
    [FieldOffset(8)] private TreasureChest _treasureChest;
}

public static class UsePickup
{
    public static long Use()
    {
        var pickup = new Pickup();
        int coin = pickup.Type == PickupType.Coin ? pickup.Coin.Value : 0;
        pickup = new Gem { Shine = 1 };
        pickup.Gem.Colour = 2;
        ref Gem gem = ref pickup.Gem;
        gem.Shine += 1;
        pickup = new TreasureChest { Contents = 3 };
        Pickup other = new Coin { Value = 4 };
        return coin + pickup.TreasureChest.Contents + other.Coin.Value + (long)pickup.Type;
    }
}
