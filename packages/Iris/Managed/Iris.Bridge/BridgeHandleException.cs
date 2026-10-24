using System;

namespace Iris.Bridge;

// Thrown when a call names a handle the bridge no longer holds (an object that was released, or one
// from another component). Exports map it to InvalidHandle, like any other stale handle.
internal sealed class BridgeHandleException(string message) : Exception(message);
