namespace Fenrir.ScriptFixture;

// The smallest script assembly: it exists so the tests have something real to load. Later tickets grow it
// into systems and components.
public static class Fixture
{
    public static int Answer() => 42;
}
