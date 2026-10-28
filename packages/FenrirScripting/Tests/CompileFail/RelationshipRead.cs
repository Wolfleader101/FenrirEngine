using Fenrir.Scripting;

namespace CompileFail;

// Reading the hierarchy is fine: through a ReadOnlyRef, with a property per field.
public static class ReadRelationship
{
    public static uint Parent(ReadOnlyRef<Relationship> relationship) =>
        relationship.Parent == Entity.None ? uint.MaxValue : relationship.Parent.Value;

    public static Relationship Copy(ReadOnlyRef<Relationship> relationship) => relationship.Value;
}
