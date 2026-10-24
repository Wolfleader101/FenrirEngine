using System;
using Iris.Examples.Dependency;
using Iris.Bridge;

namespace Iris.Examples.Component;

// Resolving this type exercises private dependency loading without constructing an object.
public sealed class ComponentMarker : DependencyMarker
{
    // Keep an actual assembly reference to the shared resident bridge for resolution checks.
    public static Type BridgeType => typeof(Bootstrap);
}
