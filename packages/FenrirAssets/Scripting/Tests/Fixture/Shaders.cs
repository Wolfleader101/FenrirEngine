using System.Globalization;
using System.Text;
using Fenrir.Assets;

namespace AssetsScriptFixture;

// A shader's parameters as C# reads them, called from the tests outside any system.
public static class ShaderCalls
{
    public static int Count(ulong shader) => Shaders.GetParameterCount(TestHandle.To<Shader>(shader));

    // Every parameter, one line each: "name type v0,v1,v2,v3 texture", the type and texture as numbers.
    public static string List(ulong shaderBits)
    {
        AssetHandle<Shader> shader = TestHandle.To<Shader>(shaderBits);
        var text = new StringBuilder();
        Span<char> name = stackalloc char[64];
        int count = Shaders.GetParameterCount(shader);
        for (int index = 0; index < count; ++index)
        {
            int length = Shaders.GetParameter(shader, index, out ShaderParameter parameter, name);
            text.Append(name[..System.Math.Min(length, name.Length)]).Append(' ').Append((int)parameter.Type).Append(' ')
                .Append(string.Join(",", new[] { parameter.Value.X, parameter.Value.Y, parameter.Value.Z, parameter.Value.W }
                                             .Select(value => value.ToString(CultureInfo.InvariantCulture))))
                .Append(' ').Append((int)parameter.Texture).Append('\n');
        }
        return text.ToString();
    }

    // What GetParameter returns, and wrote, given a span of `capacity` chars.
    public static int NameLength(ulong shader, int index, int capacity)
    {
        Span<char> name = stackalloc char[capacity];
        return Shaders.GetParameter(TestHandle.To<Shader>(shader), index, out _, name);
    }

    public static string Name(ulong shader, int index, int capacity)
    {
        Span<char> name = stackalloc char[capacity];
        int length = Shaders.GetParameter(TestHandle.To<Shader>(shader), index, out _, name);
        return new string(name[..System.Math.Min(length, capacity)]);
    }

    // Whether a parameter read that found nothing left its output zeroed.
    public static bool MissingParameterIsZeroed(ulong shader, int index)
    {
        int length = Shaders.GetParameter(TestHandle.To<Shader>(shader), index, out ShaderParameter parameter, Span<char>.Empty);
        return length == 0 && parameter.Type == ShaderParameterType.Float && parameter.Value.X == 0 &&
               parameter.Texture == DefaultTexture.White;
    }
}
