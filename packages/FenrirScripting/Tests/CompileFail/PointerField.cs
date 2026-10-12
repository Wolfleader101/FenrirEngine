using Fenrir.Scripting;

namespace CompileFail;

// A pointer is unmanaged, so a component may hold one; its accessor class must then be unsafe.
public unsafe struct Linked : IComponent
{
    public int* Next;
    public int Id;
}

public static unsafe class UseLinked
{
    public static void Use(Ref<Linked> linked)
    {
        linked.Next = null;
        linked.Id += 1;
    }
}
