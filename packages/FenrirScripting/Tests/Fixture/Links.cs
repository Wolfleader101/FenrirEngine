using System.Reflection;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// A native component with an Entity field, registered by the tests under "Test.Tether", as a joint names its bodies.
[NativeComponent("Test.Tether")]
public struct Tether : IComponent
{
    public EntityLink Target;
    public int Length;
}

// Entity links made in C#, called by the tests through Iris.
public static class Links
{
    // Links the holder's Tether to the target by assigning the Entity.
    public static void LinkTo(uint holder, uint target)
    {
        var tether = new Entity(holder).Get<Tether>();
        tether.Target = new Entity(target);
    }

    // As a system builds a joint: creates a target and a holder, and links them before either exists in storage.
    // Returns the holder.
    public static uint CreateLinkedPair()
    {
        var target = Entity.Create();
        var holder = Entity.Create();
        holder.Add(new Tether { Target = target, Length = 3 });
        return holder.Value;
    }

    // Empties the holder's link.
    public static void Unlink(uint holder)
    {
        var tether = new Entity(holder).Get<Tether>();
        tether.Target = EntityLink.None;
    }

    // Writes a zeroed link, as default(Joint) would hold.
    public static void WriteZeroed(uint holder)
    {
        var tether = new Entity(holder).Get<Tether>();
        tether.Target = default;
    }

    // The entity value the holder's link names, or uint.MaxValue when it is empty.
    public static uint TargetOf(uint holder) => Named(new Entity(holder).Get<Tether>().Target);

    public static bool NoneIsNull() => EntityLink.None.IsNull;

    // What a script can read off an EntityLink: its public fields and properties, as "Name:Type", sorted.
    public static string? ReadableMembers()
    {
        const BindingFlags everyPublic =
            BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        var fields = typeof(EntityLink).GetFields(everyPublic).Select(field => $"{field.Name}:{field.FieldType.Name}");
        var properties = typeof(EntityLink)
                             .GetProperties(everyPublic)
                             .Select(property => $"{property.Name}:{property.PropertyType.Name}");
        return string.Join(",", fields.Concat(properties).Order(StringComparer.Ordinal));
    }

    private static uint Named(in EntityLink link) => link.IsNull ? uint.MaxValue : link.Target.Value;
}
