using System;
using System.Text;
using Iris.Bridge;

// Blittable structs through the real RegisterStruct / FindMethod / InvokeMethod / member exports.
static unsafe class StructChecks
{
    private const string Component = "Iris.Examples.Component.";
    private const int Float = 10, Double = 11, Int32 = 6, UInt8 = 3;
    private const int StructKind = 14;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static string Text(byte[] storage, int written) => new UTF8Encoding(false, true).GetString(storage, 0, written);

    private static int Register(ulong token, string description, out ulong id, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong*, byte*, int, int*, int*, int> register = &Bootstrap.RegisterStruct;
        byte[] bytes = Encoding.UTF8.GetBytes(description), storage = new byte[4096];
        fixed (byte* text = bytes, diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = register(token, text, bytes.Length, &value, diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed registration published a handle");
            id = value;
            diagnostic = Text(storage, written);
            return status;
        }
    }

    private const string Vec3 = Component + "Vec3";

    private static string Vec3Description(string first = "X", string second = "Y", int size = 12, int secondOffset = 4) =>
        $"{Vec3}|{size}\n{first}|0|{Float}|\n{second}|{secondOffset}|{Float}|\nZ|8|{Float}|";

    private static ulong Registered(ulong token, string description)
    {
        Check(Register(token, description, out ulong id, out string diagnostic) == 0 && id != 0, "Registration failed: " + diagnostic);
        return id;
    }

    private static int KindOf(ulong id) => (int)(id << 8) | StructKind;

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static ulong Method(ulong token, ulong type, string name, int returnKind, params int[] parameters)
    {
        Check(ComponentRegistry.TryFindMethod(token, type, name, returnKind, parameters, true, out ulong id) == ComponentRegistry.FindResult.Found, "Method lookup failed: " + name);
        return id;
    }

    private static ulong[] Vec(ulong token, ulong method, float ax, float ay, float az, float bx, float by, float bz, out float[] result)
    {
        float[] left = { ax, ay, az }, right = { bx, by, bz };
        fixed (float* l = left, r = right)
        {
            Check(ComponentRegistry.TryInvokeMethod(token, method, new[] { (ulong)l, (ulong)r }, out ulong block), "Invocation failed");
            result = new float[3];
            for (int index = 0; index < 3; ++index) result[index] = ((float*)block)[index];
            ValueKinds.FreeString(block);
        }
        return Array.Empty<ulong>();
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path), other = ComponentRegistry.Load(path);
        try
        {
            // A matching mapping registers, and registering again returns the same handle.
            ulong vec3 = Registered(token, Vec3Description());
            Check(Registered(token, Vec3Description()) == vec3, "Registering again created another handle");
            Check(Registered(other, Vec3Description()) != 0, "A second component could not register the same struct");

            // Mismatches are diagnosed and nothing is registered.
            Check(Register(token, Vec3Description(size: 24), out ulong none, out string sizeProblem) == 1 && none == 0 && sizeProblem.Contains("12") && sizeProblem.Contains("24"),
                  "A size mismatch was accepted or undiagnosed: " + sizeProblem);
            Check(Register(token, Vec3Description(first: "Y", second: "X"), out none, out string orderProblem) == 1 && orderProblem.Contains("order"),
                  "A field-order mismatch was accepted or undiagnosed: " + orderProblem);
            Check(Register(token, Vec3Description(secondOffset: 8), out none, out string offsetProblem) == 1 && offsetProblem.Contains("offset"),
                  "An offset mismatch was accepted or undiagnosed: " + offsetProblem);
            Check(Register(token, $"{Vec3}|12\nX|0|{Double}|\nY|4|{Float}|\nZ|8|{Float}|", out none, out string typeProblem) == 1 && typeProblem.Contains("type"),
                  "A field type mismatch was accepted or undiagnosed: " + typeProblem);
            Check(Register(token, $"{Vec3}|12\nX|0|{Float}|\nY|4|{Float}|", out none, out string countProblem) == 1 && countProblem.Contains("fields"),
                  "A field count mismatch was accepted or undiagnosed: " + countProblem);

            // Non-blittable and unusable types are rejected with a reason.
            Check(Register(token, $"{Component}WithBool|8\nFlag|0|1|\nValue|4|{Int32}|", out none, out string boolProblem) == 1 && boolProblem.Contains("blittable"),
                  "A struct with a bool was accepted: " + boolProblem);
            Check(Register(token, $"{Component}WithText|8\nName|0|{Int32}|", out none, out string textProblem) == 1 && textProblem.Contains("blittable"),
                  "A struct with a reference was accepted: " + textProblem);
            Check(Register(token, $"{Component}Counter|4\nValue|0|{Int32}|", out none, out string classProblem) == 1 && classProblem.Contains("not a struct"),
                  "A class was accepted as a struct: " + classProblem);
            Check(Register(token, $"{Component}Missing|4\nValue|0|{Int32}|", out none, out _) == 6, "A missing struct was not NotFound");
            Check(Register(token, "garbage", out none, out _) == 1, "A malformed description was accepted");
            Check(Register(0, Vec3Description(), out none, out _) == 5 && Register(token + 1000, Vec3Description(), out none, out _) == 5, "An unknown component was accepted");

            // Nested structs must be registered first, and are checked against the field's real type.
            string pose = $"{Component}Pose|40\nPosition|0|{StructKind}|{Vec3}\nScale|12|{StructKind}|{Vec3}\nWeight|24|{Double}|\nFlags|32|{UInt8}|";
            Check(Register(other, $"{Component}Pose|40\nPosition|0|{StructKind}|{Vec3}\nScale|12|{StructKind}|{Vec3}\nWeight|24|{Double}|\nFlags|32|{UInt8}|", out ulong poseId, out string nestedProblem) == 0,
                  "Nested registration failed: " + nestedProblem);
            Check(Register(token, pose.Replace("Position|0|14|" + Vec3, "Position|0|14|" + Component + "Pose"), out none, out string wrongNested) == 1 && wrongNested.Contains("Position"),
                  "A nested field with the wrong struct was accepted: " + wrongNested);
            ulong poseOnToken = Registered(token, pose);

            // Static calls with struct arguments and returns, nested structs and explicit layout.
            ulong structs = TypeId(token, Component + "Structs");
            int vecKind = KindOf(vec3);
            ulong add = Method(token, structs, "Add", vecKind, vecKind, vecKind);
            Vec(token, add, 1, 2, 3, 10, 20, 30, out float[] sum);
            Check(sum[0] == 11 && sum[1] == 22 && sum[2] == 33, "A struct did not round-trip through a call");
            ulong dot = Method(token, structs, "Dot", Float, vecKind, vecKind);
            float[] a = { 1, 2, 3 }, b = { 4, 5, 6 };
            fixed (float* ap = a, bp = b)
            {
                Check(ComponentRegistry.TryInvokeMethod(token, dot, new[] { (ulong)ap, (ulong)bp }, out ulong dotResult) && BitConverter.UInt32BitsToSingle((uint)dotResult) == 32,
                      "A struct argument was not read correctly");
            }
            Check(ComponentRegistry.TryFindMethod(token, structs, "Add", vecKind, new[] { vecKind }, true, out _) == ComponentRegistry.FindResult.NotFound,
                  "A struct signature matched by count alone");
            Check(ComponentRegistry.TryFindMethod(token, structs, "Add", Float, new[] { vecKind, vecKind }, true, out _) == ComponentRegistry.FindResult.NotFound,
                  "A struct return matched a float return");
            Check(ComponentRegistry.TryFindMethod(token, structs, "Dot", Float, new[] { Float, Float }, true, out _) == ComponentRegistry.FindResult.NotFound,
                  "A float parameter matched a struct parameter");

            // Unknown or foreign struct handles are argument errors, not crashes.
            bool threw = false;
            try { ComponentRegistry.TryFindMethod(token, structs, "Add", KindOf(9999), new[] { vecKind, vecKind }, true, out _); }
            catch (BridgeArgumentException) { threw = true; }
            Check(threw, "An unknown struct handle was accepted");

            // Null struct arguments are rejected.
            threw = false;
            try { ComponentRegistry.TryInvokeMethod(token, add, new ulong[] { 0, 0 }, out _); }
            catch (BridgeArgumentException) { threw = true; }
            Check(threw, "A null struct argument was accepted");

            // Fields and properties of struct type.
            ulong body = TypeId(token, Component + "Body");
            Check(ComponentRegistry.TryCreateObject(token, body, Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out ulong bodyObject) == ComponentRegistry.CreateResult.Created, "Body construction failed");
            foreach ((string name, int kind) in new[] { ("Position", 0), ("Velocity", 1) })
            {
                Check(ComponentRegistry.TryFindMember(token, body, name, kind, vecKind, out ulong member) == ComponentRegistry.FindResult.Found, "Struct member lookup failed: " + name);
                float[] value = { 1.5f, -2, 3 };
                fixed (float* v = value)
                {
                    Check(ComponentRegistry.TrySetMemberValue(token, member, bodyObject, (ulong)v), "Struct set failed: " + name);
                }
                Check(ComponentRegistry.TryGetMemberValue(token, member, bodyObject, out ulong block), "Struct get failed: " + name);
                Check(((float*)block)[0] == 1.5f && ((float*)block)[1] == -2 && ((float*)block)[2] == 3, "A struct member did not round-trip: " + name);
                ValueKinds.FreeString(block);
            }
            Check(ComponentRegistry.TryFindMember(token, body, "Position", 0, Float, out _) == ComponentRegistry.FindResult.NotFound, "A struct field matched a float");

            // A released component's struct kinds stop working.
            ComponentRegistry.TryRelease(other, force: true);
            Check(Register(other, Vec3Description(), out none, out _) == 5, "A released component accepted a registration");
            Check(poseId != 0 && poseOnToken != 0, "Nested structs did not register");
        }
        finally
        {
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
            ComponentRegistry.ForgetUnload(other);
        }
        Console.WriteLine("Structs: mappings are validated against the managed type, mismatches and non-blittable types are diagnosed, and structs round-trip through calls and members.");
    }
}
