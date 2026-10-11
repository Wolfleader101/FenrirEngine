namespace Iris.Examples.Component;

// Two builds of this component (see IrisFixtureVersion in the project) behave differently, so reload checks
// can tell which one is loaded.
public static class Version
{
#if FIXTURE_B
    public static int Number() => 2;
    public static string Name() => "B";
    public static int Scale(int value) => value * 10;
#else
    public static int Number() => 1;
    public static string Name() => "A";
    public static int Scale(int value) => value * 2;
#endif
}

#if FIXTURE_B
public class OnlyInB
{
}
#else
public class OnlyInA
{
}
#endif
