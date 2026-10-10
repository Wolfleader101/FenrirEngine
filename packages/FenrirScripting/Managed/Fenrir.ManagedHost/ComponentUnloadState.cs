namespace Fenrir.ManagedHost;

// int32 wire values, separate from the operation's BridgeStatus.
internal enum ComponentUnloadState : int
{
    NotRequested = 0,
    Pending = 1,
    Collected = 2
}
