using Fenrir.Scripting;

namespace CompileFail;

public static class AssignRelationshipField
{
    public static void Use(ReadOnlyRef<Relationship> relationship)
    {
        relationship.Parent = default;
    }
}
