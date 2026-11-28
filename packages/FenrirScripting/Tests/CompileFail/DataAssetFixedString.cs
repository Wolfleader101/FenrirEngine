using Fenrir.Scripting;

namespace CompileFail;

// [FixedString] marks a byte buffer, which a data asset never holds; a string is already text.
public class Loot : IDataAsset
{
    [FixedString] public string Label = "";
}
