namespace Fixture.Bindings;

// The binding's own C# types for the fixture's bound types (Fixture.binding.toml, [bound-types]), written by hand as a
// binding writes its vector, quaternion and handle. Native/BoundMappings.hpp maps them.

/// <summary>Stands for <c>Fixture::Vec3</c>; its constructor takes the components in the order the binding names.</summary>
public struct Vector3
{
    public float X, Y, Z;

    public Vector3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }
}

/// <summary>Stands for <c>Fixture::Quat</c>, set by component name.</summary>
public struct Quaternion
{
    public float X, Y, Z, W;
}

/// <summary>Stands for <c>Fixture::Ticket</c>: one member, so a native takes it by value as that member.</summary>
public struct Ticket
{
    public uint Number;
}

/// <summary>Stands for <c>Fixture::Handle</c>: new, it holds C++'s default id, all bits set.</summary>
public struct Handle
{
    public uint Id = uint.MaxValue;
    public ulong Owner;

    public Handle()
    {
    }
}
