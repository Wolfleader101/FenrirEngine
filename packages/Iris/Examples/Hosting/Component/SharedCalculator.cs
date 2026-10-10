using System.Runtime.Loader;
using Iris.Examples.Contracts;

namespace Iris.Examples.Component;

public sealed class SharedCalculator : CalculatorBase
{
    public override int Add(int left, int right) => checked(left + right);

    // 1 when the contract resolved to the host's shared copy in the default context, 0 when this
    // component loaded its own private copy.
    public static int ContractIsShared(int left, int right) =>
        AssemblyLoadContext.GetLoadContext(typeof(CalculatorBase).Assembly) == AssemblyLoadContext.Default ? 1 : 0;
}
