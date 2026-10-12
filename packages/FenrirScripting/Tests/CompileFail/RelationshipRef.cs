using Fenrir.Scripting;

namespace CompileFail;

// The hierarchy has no writable view: Ref<T> is only for IComponent, and Relationship is not one.
public static class WriteRelationshipRef
{
    public static void Use(Ref<Relationship> relationship)
    {
    }
}
