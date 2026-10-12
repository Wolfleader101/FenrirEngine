namespace Iris.Examples.Contracts;

// A native function the host provides to a shared contract. Iris binds it when the assembly is shared (the host
// must register "Iris.Examples.Contracts.SharedNatives.Triple" first), so every component that uses the contract
// reaches the same function. In a component that keeps a private copy of the contract the field is not bound.
public static unsafe class SharedNatives
{
    public static delegate* unmanaged[Cdecl]<int, int> Triple;

    public static int CallTriple(int value) => Triple(value);
}
