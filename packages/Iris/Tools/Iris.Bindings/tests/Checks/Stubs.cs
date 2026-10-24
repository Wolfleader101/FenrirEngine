// Stand-ins for the scripting SDK the fixture's binding names (Fixture.binding.toml, [csharp]), so the generated mirrors
// compile and run without it. The SDK check (Sdk/) compiles the same mirrors against the real one.

namespace Fenrir.Scripting
{
    public interface IReadOnlyComponent
    {
    }

    public interface IComponent : IReadOnlyComponent
    {
    }

    public interface IEvent
    {
    }

    [AttributeUsage(AttributeTargets.Struct)]
    public sealed class NativeComponentAttribute(string name) : Attribute
    {
        public string Name { get; } = name;
    }

    [AttributeUsage(AttributeTargets.Struct)]
    public sealed class NativeEventAttribute(string name) : Attribute
    {
        public string Name { get; } = name;
    }

    [AttributeUsage(AttributeTargets.Struct)]
    public sealed class TaggedUnionAttribute : Attribute
    {
        public string? Noun { get; set; }
    }
}

namespace Fixture.Bindings
{
    // What the SDK's tagged-union generator writes for Shape's storage: new() makes the kind whose tag value is 0, and
    // each kind converts to a Shape holding it.
    public partial struct Shape
    {
        public Shape()
        {
            _type = ShapeType.Box;
            _box = new();
        }

        public static implicit operator Shape(BoxShape value) => new() { _type = ShapeType.Box, _box = value };

        public static implicit operator Shape(SphereShape value) => new() { _type = ShapeType.Sphere, _sphere = value };
    }
}
