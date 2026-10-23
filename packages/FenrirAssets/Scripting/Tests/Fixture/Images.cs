using Fenrir.Assets;

namespace AssetsScriptFixture;

// Images and render targets as C# makes them, called from the tests outside any system.
public static class ImageCalls
{
    public static ulong AddRenderTarget(int width, int height) => Bits.Of(Assets.AddRenderTarget(width, height));

    public static bool ResizeRenderTarget(ulong image, int width, int height) =>
        Assets.ResizeRenderTarget(Bits.To<Image>(image), width, height);

    // An image of `byteCount` bytes counting up from `first`, which is width * height * 4 for a whole RGBA8 image.
    public static ulong AddImage(int width, int height, int byteCount, int first) =>
        Bits.Of(Assets.AddImage(width, height, Pattern(byteCount, first)));

    public static bool ReplaceImage(ulong image, int width, int height, int byteCount, int first) =>
        Assets.ReplaceImage(Bits.To<Image>(image), width, height, Pattern(byteCount, first));

    private static byte[] Pattern(int byteCount, int first)
    {
        var bytes = new byte[byteCount];
        for (int i = 0; i < byteCount; ++i)
            bytes[i] = (byte)(first + i);
        return bytes;
    }
}
