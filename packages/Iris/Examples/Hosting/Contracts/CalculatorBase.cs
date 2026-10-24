namespace Iris.Examples.Contracts;

// A contract the host shares with every component. Components compile against it and may even deploy
// a copy, but when the host declares it shared they all bind to one copy with one type identity.
public abstract class CalculatorBase
{
    public abstract int Add(int left, int right);
}
