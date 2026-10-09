namespace Fenrir.ManagedHost;

// Stable int32 wire values, mirrored by the native ManagedCallStatus enum.
internal enum BridgeStatus : int
{
    Success = 0,
    InvalidArgument = 1,
    Overflow = 2,
    ManagedException = 3,
    BufferTooSmall = 4,
    InvalidHandle = 5
}
