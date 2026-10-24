namespace Iris.Examples.Dependency;

public static class DependencyMath
{
    public static int Add(int left, int right) => checked(left + right);
}
