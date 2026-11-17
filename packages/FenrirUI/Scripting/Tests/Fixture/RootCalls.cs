using Fenrir.Assets;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Scripting;
using Fenrir.UI;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIScriptFixture;

// Image and world roots as C# makes them (UIRoot.CreateImageRoot, UIRoot.CreateWorldRoot), from the tests outside any
// system, named back to the tests by id (Roots.cpp).
public static class RootCalls
{
    // An image root of `width` by `height` pixels.
    public static uint MakeImageRoot(uint width, uint height) =>
        UIRoot.CreateImageRoot(new ImageRootSettings { Width = width, Height = height }).Value;

    // A world root on the quad of entity `entity` (C++ hands its id), without the default stylesheet, so its elements are
    // exactly the size they are given.
    public static uint MakeWorldRoot(uint entity) =>
        UIRoot.CreateWorldRoot(new WorldRootSettings { Entity = new Entity(entity), DefaultStylesheet = false }).Value;

    // A root on the main window's overlay, at layer 3.
    public static uint MakeWindowRoot() => UIRoot.Create(new RootSettings { Window = new WindowId { Value = 1 }, Layer = 3 }).Value;

    // The root's image, packed with its index in the high half and its generation in the low.
    public static ulong Image(uint value)
    {
        AssetHandle<Image> image = Ids.Root(value).GetImage();
        return ((ulong)image.Index << 32) | image.Generation;
    }

    // A label reading `text` under the root element.
    public static ulong AddLabel(uint value, string text)
    {
        UIRoot root = Ids.Root(value);
        ElementId label = root.CreateElement(root.GetRootElement(), "label", -1);
        root.SetText(label, text);
        return Ids.Pack(label);
    }

    // Gives the root a ray from 5 in front of its quad's centre, straight at it, with the left button `pressed`: whether
    // the pointer is over an element.
    public static bool AimAtCentre(uint value, bool pressed)
    {
        var ray = new Ray { Origin = new Vector3(0, 0, 5), Direction = new Vector3(0, 0, -1) };
        return Ids.Root(value).SetPointerRay(ray, pressed);
    }

    // Takes the root's ray away: whether the pointer is still over an element.
    public static bool ClearRay(uint value) => Ids.Root(value).ClearPointerRay();
}
