namespace Fenrir.HostingDependency;

public static class DependencyMath
{
    public static int Add(int left, int right) => checked(left + right);
}
