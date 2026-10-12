namespace Iris.Examples.Contracts;

// A struct that lives in the shared contract, not in any component: a host maps it by name, and its fields are named
// differently from the C++ members (see IRIS_FIELD_AS).
public struct SharedPoint
{
    public int X;
    public int Renamed;
}
