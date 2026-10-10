using System;

namespace Iris.Bridge;

// Thrown only by the bridge's own argument validation. Exports map it to InvalidArgument; any other
// exception, including an ArgumentException from the runtime or a component, is ManagedException.
internal sealed class BridgeArgumentException(string message) : ArgumentException(message);
