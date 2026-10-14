using System;
using System.Text;

namespace Iris.Bridge;

// How an export turns an exception into a status. The exports have never agreed on this, and a caller can see the
// difference, so each export names the mapping it has always had (see the table in ExportContractChecks).
internal enum ExceptionMapping
{
    // Every exception is ManagedException: there is no InvalidArgument and no InvalidHandle path.
    AlwaysManaged,
    // Undecodable text and a bridge argument error are InvalidArgument; anything else is ManagedException.
    ArgumentOrDecode,
    // A bridge argument error is InvalidArgument, a stale handle exception is InvalidHandle, anything else is ManagedException.
    ByKind,
    // As ByKind, and undecodable text is InvalidArgument.
    ByKindAndDecode
}

// What an export does around its work, kept in one place: the diagnostic tail (validated, then used to report a
// failure) and the mapping of an exception or a lookup result to a status. The work of an export stays in the export.
// A tail is built from the export's four diagnostic arguments; none of them is retained past the call.
internal readonly unsafe struct DiagnosticTail(byte* buffer, int capacity, int* written, int* truncated)
{
    // Clears the metadata outputs even when another argument is invalid; false when the tail itself cannot be used.
    internal bool IsValid() => ManagedDiagnostic.ValidateOutput(buffer, capacity, written, truncated);

    // Writes the exception's details (and reports it to the log sink) and returns the status the mapping gives it.
    internal int Fail(Exception exception, ExceptionMapping mapping)
    {
        ManagedDiagnostic.Write(exception, new Span<byte>(buffer, capacity), out *written, out *truncated);
        return (int)ExportEnvelope.StatusOf(exception, mapping);
    }

    // Writes an explanation for a failure the bridge itself found, without reporting it as an error to the log sink.
    internal void Explain(Exception explanation)
        => ManagedDiagnostic.Write(explanation, new Span<byte>(buffer, capacity), out *written, out *truncated, report: false);
}

internal static unsafe class ExportEnvelope
{
    internal static BridgeStatus StatusOf(Exception exception, ExceptionMapping mapping) => mapping switch
    {
        ExceptionMapping.AlwaysManaged => BridgeStatus.ManagedException,
        ExceptionMapping.ArgumentOrDecode => exception is DecoderFallbackException or BridgeArgumentException
            ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException,
        ExceptionMapping.ByKind => ByKind(exception),
        ExceptionMapping.ByKindAndDecode => exception is DecoderFallbackException
            ? BridgeStatus.InvalidArgument : ByKind(exception),
        _ => throw new ArgumentOutOfRangeException(nameof(mapping))
    };

    // Bridge validation is InvalidArgument and a stale handle is InvalidHandle; anything else, including every
    // exception from component code, is ManagedException.
    private static BridgeStatus ByKind(Exception exception) => exception switch
    {
        BridgeArgumentException => BridgeStatus.InvalidArgument,
        BridgeHandleException => BridgeStatus.InvalidHandle,
        _ => BridgeStatus.ManagedException
    };

    // The status of a lookup's result, for every export that looks something up. Found is Success; NotFound means
    // nothing matched (the export explains it with DiagnosticTail.Explain).
    internal static BridgeStatus StatusOf(ComponentRegistry.FindResult result) => result switch
    {
        ComponentRegistry.FindResult.Found => BridgeStatus.Success,
        ComponentRegistry.FindResult.InvalidHandle => BridgeStatus.InvalidHandle,
        _ => BridgeStatus.NotFound
    };

    // An explicit-length UTF-8 argument as text, strictly: undecodable bytes throw DecoderFallbackException.
    internal static string Decode(byte* text, int length)
        => new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(text, length));

    // A name: decoded strictly, and refused (a bridge argument error) if it holds a null character. `what` names it
    // in the explanation, for example "type name".
    internal static string DecodeName(byte* text, int length, string what)
    {
        string name = Decode(text, length);
        if (name.Contains('\0')) throw new BridgeArgumentException($"A {what} cannot contain null characters.");
        return name;
    }
}
