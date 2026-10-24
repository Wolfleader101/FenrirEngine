using Fenrir.Scripting;

namespace CompileFail;

public static class AssignRelationship
{
    public static void Use(ReadOnlyRef<Relationship> relationship)
    {
        relationship.Value = default;
    }
}
