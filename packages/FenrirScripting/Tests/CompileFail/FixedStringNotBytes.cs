using Fenrir.Scripting;

namespace CompileFail;

// [FixedString] marks UTF-8 text in a byte buffer; on a buffer of ints it would describe bytes that are no text.
public unsafe struct Named : IComponent
{
    [FixedString] public fixed int Text[4];
}
