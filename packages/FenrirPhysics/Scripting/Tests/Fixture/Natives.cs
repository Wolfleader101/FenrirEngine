using System.Reflection;
using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// The physics native functions that write an output, called straight through the function pointers the host bound,
// with that output already holding junk. The generated C# side of a native sets an out parameter to default before
// calling, so only a direct call shows whether the native itself wrote its output. The natives are internal to the
// contract, so they are reached by reflection. A bool crosses as a byte: this assembly keeps runtime marshalling on.
public static unsafe class Natives
{
    private static readonly Type physicsNative =
        typeof(PhysicsWorld).Assembly.GetType("Fenrir.Physics.PhysicsNative", throwOnError: true)!;

    // An entity no scene has: a body that is not alive.
    private const uint NoSuchEntity = 0x00ABCDEFu;

    // How many of the contract's natives have an out parameter, so a new one is not left out of LeavesOutputDefault.
    public static int OutputNativeCount() =>
        physicsNative.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
            .Count(method => method.GetParameters().Any(parameter => parameter.IsOut));

    // Whether native `which` (in name order: Bounds, Depenetrate, FrameFromWorld, GetJointState, Gravity, MoveCast,
    // Raycast, ShapeCast), called with null inputs and entities that are not alive, wrote the C++ default to its output:
    // what the C# struct's parameterless constructor gives.
    public static bool LeavesOutputDefault(int which)
    {
        switch (which)
        {
            case 0:
            {
                Aabb bounds = Junk<Aabb>();
                ((delegate* unmanaged[Cdecl]<uint, Aabb*, byte>)Bound("Bounds"))(NoSuchEntity, &bounds);
                return IsNew(bounds);
            }
            case 1:
            {
                Vector3 offset = Junk<Vector3>();
                ((delegate* unmanaged[Cdecl]<void*, void*, void*, Vector3*, byte>)Bound("Depenetrate"))(null, null, null,
                                                                                                       &offset);
                return IsNew(offset);
            }
            case 2:
            {
                JointFrames frames = Junk<JointFrames>();
                ((delegate* unmanaged[Cdecl]<uint, uint, void*, JointFrames*, void>)Bound("FrameFromWorld"))(
                    NoSuchEntity, NoSuchEntity, null, &frames);
                return IsNew(frames);
            }
            case 3:
            {
                JointState state = Junk<JointState>();
                ((delegate* unmanaged[Cdecl]<uint, JointState*, void>)Bound("GetJointState"))(NoSuchEntity, &state);
                return IsNew(state);
            }
            case 4:
            {
                Vector3 gravity = Junk<Vector3>();
                ((delegate* unmanaged[Cdecl]<Vector3*, byte>)Bound("Gravity"))(&gravity);
                return IsNew(gravity);
            }
            case 5:
            {
                MoveCastHit hit = Junk<MoveCastHit>();
                ((delegate* unmanaged[Cdecl]<void*, void*, void*, void*, MoveCastHit*, byte>)Bound("MoveCast"))(
                    null, null, null, null, &hit);
                return IsNew(hit);
            }
            case 6:
            {
                RaycastHit hit = Junk<RaycastHit>();
                ((delegate* unmanaged[Cdecl]<void*, void*, float, void*, RaycastHit*, byte>)Bound("Raycast"))(
                    null, null, 0.0f, null, &hit);
                return IsNew(hit);
            }
            case 7:
            {
                ShapeCastHit hit = Junk<ShapeCastHit>();
                ((delegate* unmanaged[Cdecl]<void*, void*, void*, float, void*, ShapeCastHit*, byte>)Bound("ShapeCast"))(
                    null, null, null, 0.0f, null, &hit);
                return IsNew(hit);
            }
            default:
                throw new ArgumentOutOfRangeException(nameof(which));
        }
    }

    // The function pointer the host bound for the native `method`.
    private static void* Bound(string method)
    {
        object? value = physicsNative.GetField("__native_" + method, BindingFlags.Static | BindingFlags.Public)!
            .GetValue(null);
        return value is IntPtr address ? (void*)address : Pointer.Unbox(value!);
    }

    // A T whose every byte is 0x5A: no field reads as its default.
    private static T Junk<T>() where T : unmanaged
    {
        T value;
        new Span<byte>(&value, sizeof(T)).Fill(0x5A);
        return value;
    }

    private static bool IsNew<T>(T value) where T : unmanaged => EqualityComparer<T>.Default.Equals(value, new T());
}
