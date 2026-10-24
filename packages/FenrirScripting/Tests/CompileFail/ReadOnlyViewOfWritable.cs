using Fenrir.Scripting;

namespace CompileFail;

// A writable component seen through the cautious view cannot be written either.
public static class WriteThroughReadOnly
{
    public static void Use(ReadOnlyRef<Transform> transform)
    {
        transform.Position.X = 1;
    }
}
