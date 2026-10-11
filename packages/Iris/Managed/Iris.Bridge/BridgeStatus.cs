namespace Iris.Bridge;

// Stable int32 wire values, mirrored by the native CallStatus enum (except BufferTooSmall, which native code
// reports as InvalidResponse because it always sizes buffers from the bridge's own answer).
internal enum BridgeStatus : int
{
    Success = 0,
    InvalidArgument = 1,
    // 2 was Overflow; component exceptions are all ManagedException. The value stays unused.
    ManagedException = 3,
    BufferTooSmall = 4,
    InvalidHandle = 5,
    NotFound = 6,
    InUse = 7
}
