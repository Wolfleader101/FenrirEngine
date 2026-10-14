using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Iris.Bridge;

// What a caller of each bridge export can get, one table row per export: the status for a bad diagnostic tail, for a
// bad name, for an argument the export rejects and for a stale handle, and what the outputs hold afterwards. The
// table is the record of the exports' error contract, so a change to how the exports are written has to keep it.
//
// Every case runs without a loaded component: the cases are the ones the export decides before it reaches a handle,
// plus two exports run on a file that cannot be read. `Mapping` and `Reason` are recorded from the exports' source, to
// make the differences between them a stated fact; they are not run, because an exception from component code needs a
// component, and the invocation, objects and members groups already pin those.
static class ExportContractChecks
{
    // The failures of the row cases are gathered, so one run shows every export that disagrees with the table.
    private static readonly System.Collections.Generic.List<string> Failed = new();
    private static bool Collecting;

    private static void Check(bool condition, string message)
    {
        if (condition) return;
        if (!Collecting) throw new Exception(message);
        Failed.Add(message);
    }

    // What an export leaves in the output a case primed with 99.
    private enum Out { None, HandleZero, CountZero, StateMinusOne, LengthZero }

    // How an export maps an exception to a status. The exports do not agree, and the differences are kept on purpose
    // (see the notes), so changing one is a visible change to this table.
    private enum Mapping
    {
        // Every exception, whatever it is, is ManagedException: there is no InvalidArgument and no InvalidHandle path.
        AlwaysManaged,
        // A bad name (undecodable text, a null character) or a bridge argument error is InvalidArgument; any other
        // exception is ManagedException; there is no InvalidHandle path.
        ArgumentElseManaged,
        // A bridge argument error is InvalidArgument, a stale handle exception is InvalidHandle, and any other
        // exception is ManagedException.
        ByKind,
        // As ByKind, except that undecodable text is InvalidArgument.
        ByKindAndDecode,
        // The export reports nothing: it has no failure to report.
        Cannot
    }

    private enum Mode { Valid, BadArgument }

    // The wire statuses a case expects (BridgeStatus is the bridge's own enum, so the table does not borrow its values).
    private const int InvalidArgument = (int)BridgeStatus.InvalidArgument;
    private const int ManagedException = (int)BridgeStatus.ManagedException;
    private const int InvalidHandle = (int)BridgeStatus.InvalidHandle;

    private unsafe struct Args
    {
        public ulong Token;
        public byte* Name;
        public int NameLength;
        public byte* Diag;
        public int Capacity;
        public int* Written;
        public int* Truncated;
        public void* Output;
        public Mode Mode;
    }

    private unsafe delegate int ExportCall(BridgeFunctions* table, Args a);

    private sealed record Row(
        string Name,
        Out Output,
        Mapping Mapping,
        string Reason,
        ExportCall Call,
        bool TakesName = false,
        bool TakesHandle = true,
        // Whether a name with a null character reaches the export's own check (the status is then 1) or passes it.
        bool RejectsNul = true,
        // The status for the BadArgument mode, or 0 for an export with no argument it rejects before a handle.
        int BadArgumentStatus = 0);

    private static unsafe Row[] Rows() => new[]
    {
        new Row("LoadComponent", Out.HandleZero, Mapping.ByKindAndDecode,
            "Undecodable text is InvalidArgument, a path that is not absolute is a bridge argument error (InvalidArgument), and a file that cannot be loaded is ManagedException.",
            (t, a) => t->LoadComponent(a.Name, a.NameLength, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, TakesHandle: false),
        new Row("CopyComponentNameUtf8", Out.CountZero, Mapping.AlwaysManaged,
            "Has no argument error after the buffers are checked, and no stale handle exception: a stale token is a status, not an exception.",
            (t, a) => t->CopyComponentNameUtf8(a.Token, null, 0, (int*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("ReleaseComponent", Out.None, Mapping.AlwaysManaged,
            "A stale token is a status (InvalidHandle), not an exception; every exception is ManagedException.",
            (t, a) => t->ReleaseComponent(a.Token, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("GetComponentUnloadState", Out.StateMinusOne, Mapping.AlwaysManaged,
            "The state is -1 on every failure. A stale token is a status, not an exception.",
            (t, a) => t->GetComponentUnloadState(a.Token, (int*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("ForgetComponentUnload", Out.None, Mapping.AlwaysManaged,
            "A stale token is a status, not an exception.",
            (t, a) => t->ForgetComponentUnload(a.Token, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("RegisterSharedAssembly", Out.None, Mapping.ArgumentElseManaged,
            "A path that cannot be decoded, is not absolute or has a null character is InvalidArgument; a file that cannot be read is ManagedException.",
            (t, a) => t->RegisterSharedAssembly(a.Name, a.NameLength, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, TakesHandle: false),
        new Row("FindType", Out.HandleZero, Mapping.ByKind,
            "The name is checked first, with its own mapping: undecodable text or a null character is InvalidArgument, anything else ManagedException.",
            (t, a) => t->FindType(a.Token, a.Name, a.NameLength, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true),
        new Row("CopyTypeNameUtf8", Out.CountZero, Mapping.AlwaysManaged,
            "A field other than 0 or 1 is InvalidArgument before any handle is read; a stale handle is a status.",
            (t, a) => t->CopyTypeNameUtf8(a.Token, 1, a.Mode == Mode.BadArgument ? 2 : 0, null, 0, (int*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
        new Row("FindMethod", Out.HandleZero, Mapping.ByKind,
            "The name and kinds are checked first, with their own mapping: undecodable text, a null character or an unsupported kind is InvalidArgument, anything else ManagedException.",
            (t, a) => t->FindMethod(a.Token, 1, a.Name, a.NameLength, a.Mode == Mode.BadArgument ? 999 : 0, null, 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, BadArgumentStatus: 1),
        new Row("InvokeMethod", Out.HandleZero, Mapping.ByKind,
            "Every exception from the method itself is ManagedException, including an argument or overflow exception.",
            (t, a) => t->InvokeMethod(a.Token, 1, null, a.Mode == Mode.BadArgument ? -1 : 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
        new Row("SetLogSink", Out.None, Mapping.Cannot,
            "Registers or clears the sink; the only failure is a bad diagnostic tail.",
            (t, a) => t->SetLogSink(null, null, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesHandle: false),
        new Row("ForceReleaseComponent", Out.None, Mapping.AlwaysManaged,
            "As ReleaseComponent.",
            (t, a) => t->ForceReleaseComponent(a.Token, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("FindInstanceMethod", Out.HandleZero, Mapping.ByKind,
            "As FindMethod.",
            (t, a) => t->FindInstanceMethod(a.Token, 1, a.Name, a.NameLength, a.Mode == Mode.BadArgument ? 999 : 0, null, 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, BadArgumentStatus: 1),
        new Row("InvokeInstanceMethod", Out.HandleZero, Mapping.ByKind,
            "As InvokeMethod; an object that is not an instance of the declaring type is InvalidArgument.",
            (t, a) => t->InvokeInstanceMethod(a.Token, 1, 1, null, a.Mode == Mode.BadArgument ? -1 : 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
        new Row("CreateObject", Out.HandleZero, Mapping.ByKind,
            "A constructor that throws is ManagedException and roots nothing.",
            (t, a) => t->CreateObject(a.Token, 1, null, null, a.Mode == Mode.BadArgument ? -1 : 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
        new Row("ReleaseObject", Out.None, Mapping.AlwaysManaged,
            "A stale object is a status (InvalidHandle), not an exception.",
            (t, a) => t->ReleaseObject(a.Token, 1, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("FindMember", Out.HandleZero, Mapping.ByKind,
            "The name and kind are checked first and any other exception there is ManagedException, with no InvalidHandle path; a member kind other than 0 or 1 is InvalidArgument before that.",
            (t, a) => t->FindMember(a.Token, 1, a.Name, a.NameLength, a.Mode == Mode.BadArgument ? 2 : 0, 6, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, BadArgumentStatus: 1),
        new Row("GetMember", Out.HandleZero, Mapping.ByKind,
            "An exception from a property getter is ManagedException.",
            (t, a) => t->GetMember(a.Token, 1, 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("SetMember", Out.None, Mapping.ByKind,
            "As GetMember; a read-only member is InvalidArgument.",
            (t, a) => t->SetMember(a.Token, 1, 0, 0, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("RegisterStruct", Out.HandleZero, Mapping.ByKindAndDecode,
            "Undecodable text is InvalidArgument, but the export does not check the text for a null character itself: that is left to the description's parser.",
            (t, a) => t->RegisterStruct(a.Token, a.Name, a.NameLength, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, RejectsNul: false),
        new Row("CreateArray", Out.HandleZero, Mapping.ByKind,
            "A negative length or a wrong element type is InvalidArgument.",
            (t, a) => t->CreateArray(a.Token, (6 << 8) | 15, 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("GetArrayLength", Out.LengthZero, Mapping.ByKind,
            "As CreateArray.",
            (t, a) => t->GetArrayLength(a.Token, 1, (long*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("GetArrayElement", Out.HandleZero, Mapping.ByKind,
            "An index outside the array is InvalidArgument.",
            (t, a) => t->GetArrayElement(a.Token, 1, (6 << 8) | 15, 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("SetArrayElement", Out.None, Mapping.ByKind,
            "As GetArrayElement.",
            (t, a) => t->SetArrayElement(a.Token, 1, (6 << 8) | 15, 0, 0, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("CopyArray", Out.None, Mapping.ByKind,
            "A direction other than 0 or 1 is InvalidArgument before any handle is read.",
            (t, a) => t->CopyArray(a.Token, 1, (6 << 8) | 15, 0, 0, null, a.Mode == Mode.BadArgument ? 2 : 0, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
        new Row("RegisterInternalCall", Out.None, Mapping.ByKindAndDecode,
            "Undecodable text is InvalidArgument; a null character, an unsupported kind or a conflicting registration is a bridge argument error (InvalidArgument); any other exception is ManagedException.",
            (t, a) => t->RegisterInternalCall(a.Name, a.NameLength, 0, null, a.Mode == Mode.BadArgument ? -1 : 0, (void*)1, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, TakesHandle: false, BadArgumentStatus: 1),
        new Row("ListTypes", Out.HandleZero, Mapping.ByKind,
            "No component code runs.",
            (t, a) => t->ListTypes(a.Token, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated)),
        new Row("QueryType", Out.HandleZero, Mapping.ByKind,
            "A query other than 0 to 5 is InvalidArgument before any handle is read. No component code runs.",
            (t, a) => t->QueryType(a.Token, 1, a.Mode == Mode.BadArgument ? 99 : 0, (ulong*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
        new Row("IsTypeAssignable", Out.CountZero, Mapping.ByKindAndDecode,
            "Exactly one of the other type's handle and a name must be given (else InvalidArgument); a name with a null character is InvalidArgument.",
            (t, a) => t->IsTypeAssignable(a.Token, 1, a.Mode == Mode.BadArgument ? 1UL : 0UL, a.Name, a.NameLength, (int*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            TakesName: true, BadArgumentStatus: 1),
        new Row("WaitForComponentUnload", Out.StateMinusOne, Mapping.AlwaysManaged,
            "A negative timeout is InvalidArgument; the state is -1 on every failure.",
            (t, a) => t->WaitForComponentUnload(a.Token, a.Mode == Mode.BadArgument ? -1 : 0, (int*)a.Output, a.Diag, a.Capacity, a.Written, a.Truncated),
            BadArgumentStatus: 1),
    };

    // 30 rows: the thirty-first export, FreeString, returns nothing and cannot fail.
    private static unsafe BridgeFunctions* FilledTable()
    {
        var table = (BridgeFunctions*)NativeMemory.AllocZeroed((nuint)sizeof(BridgeFunctions));
        int version = 0, reported = 0;
        delegate* unmanaged[Cdecl]<int, void*, int, int*, int*, int> handshake = &Bootstrap.GetBridgeFunctions;
        Check(handshake(BridgeFunctions.Version, table, sizeof(BridgeFunctions), &version, &reported) == 0,
            "The handshake did not fill the table");
        return table;
    }

    private static unsafe bool OutputIs(Out kind, void* output) => kind switch
    {
        Out.None => true,
        Out.HandleZero => *(ulong*)output == 0,
        Out.CountZero => *(int*)output == 0,
        Out.StateMinusOne => *(int*)output == -1,
        Out.LengthZero => *(long*)output == 0,
        _ => false
    };

    private static string ExpectedOutput(Out kind) => kind switch
    {
        Out.StateMinusOne => "-1",
        Out.None => "nothing",
        _ => "zero"
    };

    internal static unsafe void Run()
    {
        BridgeFunctions* table = FilledTable();
        Collecting = true;
        Failed.Clear();
        Row[] rows = Rows();
        Check(rows.Length == 30, "The table must have one row for each export that returns a status");
        byte[] diagnostic = new byte[4096];
        byte[] validName = Encoding.UTF8.GetBytes("Rules.Name");
        var strictUtf8 = new UTF8Encoding(false, true);
        long* output = (long*)NativeMemory.Alloc(16);
        try
        {
            fixed (byte* diag = diagnostic, name = validName)
            {
                foreach (Row row in rows)
                {
                    // A bad diagnostic tail is InvalidArgument and writes nothing, whatever else is given.
                    int written, truncated;
                    foreach ((string what, int capacity, bool nullWritten, bool nullTruncated, bool nullBuffer) in new[]
                    {
                        ("a missing count of bytes written", 16, true, false, false),
                        ("a missing truncation flag", 16, false, true, false),
                        ("a negative capacity", -1, false, false, false),
                        ("a missing buffer with a capacity", 16, false, false, true)
                    })
                    {
                        written = 99;
                        truncated = 99;
                        *output = 99;
                        Args tail = Valid(row, name, validName.Length, output);
                        tail.Diag = nullBuffer ? null : diag;
                        tail.Capacity = capacity;
                        tail.Written = nullWritten ? null : &written;
                        tail.Truncated = nullTruncated ? null : &truncated;
                        int status = row.Call(table, tail);
                        Check(status == InvalidArgument, $"{row.Name}: {what} gave status {status}, not InvalidArgument");
                        Check((nullWritten || written == 0) && (nullTruncated || truncated == 0),
                            $"{row.Name}: {what} left the metadata outputs unset");
                        Check(OutputIs(row.Output, output),
                            $"{row.Name}: {what} left the output as {*output}, not {ExpectedOutput(row.Output)}");
                    }

                    // A name the export decides is bad is InvalidArgument and leaves the outputs as on any failure.
                    if (row.TakesName)
                    {
                        foreach ((string what, byte[]? bytes, int length, int expected) in new (string, byte[]?, int, int)[]
                        {
                            ("a null name", null, 4, InvalidArgument),
                            ("an empty name", validName, 0, InvalidArgument),
                            ("a name that is not UTF-8", new byte[] { 0xFF, 0xFE }, 2, InvalidArgument),
                            ("a name with a null character", Encoding.UTF8.GetBytes(Path.GetFullPath("x") + "\0y"), -1, row.RejectsNul ? InvalidArgument : InvalidHandle)
                        })
                        {
                            written = 99;
                            truncated = 99;
                            *output = 99;
                            fixed (byte* text = bytes)
                            {
                                Args a = Valid(row, text, length < 0 ? bytes!.Length : length, output);
                                a.Diag = diag;
                                a.Capacity = diagnostic.Length;
                                a.Written = &written;
                                a.Truncated = &truncated;
                                int status = row.Call(table, a);
                                Check(status == expected, $"{row.Name}: {what} gave status {status}, not {expected}");
                                if (expected == InvalidArgument)
                                {
                                    Check(OutputIs(row.Output, output),
                                        $"{row.Name}: {what} left the output as {*output}, not {ExpectedOutput(row.Output)}");
                                }
                            }
                        }
                    }

                    // An argument the export rejects before it reads a handle.
                    if (row.BadArgumentStatus != 0)
                    {
                        written = 99;
                        truncated = 99;
                        *output = 99;
                        Args a = Valid(row, name, validName.Length, output);
                        a.Mode = Mode.BadArgument;
                        a.Diag = diag;
                        a.Capacity = diagnostic.Length;
                        a.Written = &written;
                        a.Truncated = &truncated;
                        int status = row.Call(table, a);
                        Check(status == row.BadArgumentStatus,
                            $"{row.Name}: a rejected argument gave status {status}, not {row.BadArgumentStatus}");
                        Check(OutputIs(row.Output, output),
                            $"{row.Name}: a rejected argument left the output as {*output}, not {ExpectedOutput(row.Output)}");
                    }

                    // A handle the bridge does not know is InvalidHandle. It is not an exception, so nothing is written.
                    if (row.TakesHandle)
                    {
                        written = 99;
                        truncated = 99;
                        *output = 99;
                        Args a = Valid(row, name, validName.Length, output);
                        a.Token = 0xDEADBEEF;
                        a.Diag = diag;
                        a.Capacity = diagnostic.Length;
                        a.Written = &written;
                        a.Truncated = &truncated;
                        int status = row.Call(table, a);
                        Check(status == InvalidHandle, $"{row.Name}: a stale token gave status {status}, not InvalidHandle");
                        Check(written == 0 && truncated == 0,
                            $"{row.Name}: a stale token wrote a diagnostic, but it is a status and not an exception");
                        Check(OutputIs(row.Output, output),
                            $"{row.Name}: a stale token left the output as {*output}, not {ExpectedOutput(row.Output)}");
                    }

                                    }

                // The two exports that reach component-independent code which can throw something that is not a
                // bridge argument error: the mapping is ManagedException with a diagnostic, and the outputs hold.
                string missing = Path.Combine(Path.GetTempPath(), "iris-missing-" + Guid.NewGuid().ToString("N") + ".dll");
                byte[] missingPath = Encoding.UTF8.GetBytes(missing);
                fixed (byte* path = missingPath)
                {
                    foreach (string export in new[] { "LoadComponent", "RegisterSharedAssembly" })
                    {
                        Row row = Array.Find(rows, item => item.Name == export)!;
                        int written = 99, truncated = 99;
                        *output = 99;
                        Args a = Valid(row, path, missingPath.Length, output);
                        a.Diag = diag;
                        a.Capacity = diagnostic.Length;
                        a.Written = &written;
                        a.Truncated = &truncated;
                        int status = row.Call(table, a);
                        Check(status == ManagedException, $"{export}: a file that cannot be read gave status {status}, not ManagedException");
                        Check(written > 0 && truncated == 0 && strictUtf8.GetString(diagnostic.AsSpan(0, written)).Length > 0,
                            $"{export}: a managed exception wrote no diagnostic");
                        Check(OutputIs(row.Output, output), $"{export}: a managed exception left the output set");
                    }
                }
            }
        }
        finally
        {
            NativeMemory.Free(output);
            NativeMemory.Free(table);
        }
        Collecting = false;
        if (Failed.Count > 0) throw new Exception(string.Join(Environment.NewLine, Failed));
        Console.WriteLine("Export contracts: " + rows.Length + " exports, each with its tail, name, argument and stale handle outcomes.");
    }

    private static unsafe Args Valid(Row row, byte* name, int nameLength, void* output) => new Args
    {
        Token = 1,
        Name = name,
        NameLength = nameLength,
        Output = output,
        Mode = Mode.Valid
    };
}
