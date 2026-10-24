using System;
using System.Text;
using Iris.Bridge;

// Type listing, snapshots and assignability through the registry behind the ListTypes / QueryType / IsTypeAssignable exports.
static unsafe class TypeQueryChecks
{
    private const string Component = "Iris.Examples.Component.";

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static bool Throws<T>(Action action) where T : Exception
    {
        try { action(); return false; }
        catch (T) { return true; }
    }

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static string NameOf(ulong token, ulong id)
    {
        Check(ComponentRegistry.TryGetTypeName(token, id, 0, out string? name), "Name lookup failed");
        return name!;
    }

    // A tiny reader for the snapshot format.
    private sealed class Reader(byte[] data)
    {
        private int position;
        internal int Int32() { int value = BitConverter.ToInt32(data, position); position += 4; return value; }
        internal long Int64() { long value = BitConverter.ToInt64(data, position); position += 8; return value; }
        internal string String() { int length = Int32(); string text = Encoding.UTF8.GetString(data, position, length); position += length; return text; }
        internal bool AtEnd => position == data.Length;
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path), other = ComponentRegistry.Load(path);
        try
        {
            // Listing: public non-generic types in declaration order, matching what FindType can find.
            Check(ComponentRegistry.TryListTypes(token, out ulong[] ids) && ids.Length > 10, "Listing returned too few types");
            string[] names = Array.ConvertAll(ids, id => NameOf(token, id));
            Check(Array.IndexOf(names, Component + "Actor") >= 0 && Array.IndexOf(names, Component + "Player") >= 0, "A public type was missing");
            Check(Array.IndexOf(names, Component + "Actor") < Array.IndexOf(names, Component + "Player") &&
                  Array.IndexOf(names, Component + "Player") < Array.IndexOf(names, Component + "Enemy"), "Types were not in declaration order");
            Check(Array.IndexOf(names, Component + "HiddenActor") < 0, "An internal type was listed");
            Check(!Array.Exists(names, name => name.Contains("Box")), "A generic type was listed");
            Check(Array.TrueForAll(names, name => name.StartsWith("Iris.Examples.Component.")), "A type from elsewhere was listed");
            ComponentRegistry.TryListTypes(token, out ulong[] again);
            Check(again.AsSpan().SequenceEqual(ids), "Listing twice gave different handles");
            Check(!ComponentRegistry.TryListTypes(token + 1000, out _), "An unknown component listed types");

            // Snapshots.
            ulong player = TypeId(token, Component + "Player"), actor = TypeId(token, Component + "Actor");
            Check(ComponentRegistry.TryQueryType(token, player, TypeQueries.Query.Base, out byte[] baseData), "Base query failed");
            var reader = new Reader(baseData);
            Check(reader.Int32() == 1 && reader.String() == Component + "Actor" && reader.String() == "Iris.Examples.Component" && reader.AtEnd, "The base type was wrong");
            ComponentRegistry.TryQueryType(token, TypeId(token, Component + "Plain"), TypeQueries.Query.Base, out byte[] plainBase);
            Check(new Reader(plainBase).Int32() == 1, "A static class has the object base");
            ComponentRegistry.TryQueryType(token, player, TypeQueries.Query.Interfaces, out byte[] interfaceData);
            reader = new Reader(interfaceData);
            int count = reader.Int32();
            var interfaces = new System.Collections.Generic.List<string>();
            for (int index = 0; index < count; ++index) { interfaces.Add(reader.String()); reader.String(); }
            Check(interfaces.Contains("System.IDisposable") && interfaces.Contains("Iris.Examples.Contracts.IScript"), "Interfaces were incomplete: " + string.Join(",", interfaces));
            Check(interfaces.IndexOf("Iris.Examples.Contracts.IScript") < interfaces.IndexOf("System.IDisposable"), "Interfaces were not sorted");
            ComponentRegistry.TryQueryType(token, player, TypeQueries.Query.Attributes, out byte[] attributeData);
            reader = new Reader(attributeData);
            Check(reader.Int32() >= 4, "Attributes were missing");
            Check(Throws<BridgeArgumentException>(() => TypeQueries.Run(typeof(string), (TypeQueries.Query)9)), "An unknown query was accepted");
            Check(!ComponentRegistry.TryQueryType(token, 99999, TypeQueries.Query.Base, out _), "An unknown type handle was queried");
            Check(!ComponentRegistry.TryQueryType(other, player, TypeQueries.Query.Base, out _), "A type handle worked on another component");

            // Assignability: by handle, by framework name, by a name in a referenced contract assembly.
            Check(ComponentRegistry.TryIsAssignable(token, player, actor, null) == ComponentRegistry.AssignResult.Assignable, "Player is an Actor");
            Check(ComponentRegistry.TryIsAssignable(token, actor, player, null) == ComponentRegistry.AssignResult.NotAssignable, "Actor is not a Player");
            Check(ComponentRegistry.TryIsAssignable(token, player, 0, "System.IDisposable") == ComponentRegistry.AssignResult.Assignable, "Player is IDisposable");
            Check(ComponentRegistry.TryIsAssignable(token, player, 0, "Iris.Examples.Contracts.IScript") == ComponentRegistry.AssignResult.Assignable, "Player is an IScript");
            Check(ComponentRegistry.TryIsAssignable(token, TypeId(token, Component + "Plain"), 0, "Iris.Examples.Contracts.IScript") == ComponentRegistry.AssignResult.NotAssignable, "Plain is not an IScript");
            Check(ComponentRegistry.TryIsAssignable(token, player, 0, "System.Object") == ComponentRegistry.AssignResult.Assignable, "Everything is an object");
            Check(ComponentRegistry.TryIsAssignable(token, player, 0, "No.Such.Type") == ComponentRegistry.AssignResult.UnknownType, "An unknown name was resolved");
            Check(ComponentRegistry.TryIsAssignable(token, 99999, actor, null) == ComponentRegistry.AssignResult.InvalidHandle, "An unknown source handle was accepted");
            Check(ComponentRegistry.TryIsAssignable(token, player, 99999, null) == ComponentRegistry.AssignResult.InvalidHandle, "An unknown target handle was accepted");
        }
        finally
        {
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
            ComponentRegistry.TryRelease(other, force: true);
            ComponentRegistry.ForgetUnload(other);
        }
        Console.WriteLine("Type queries: listing, base types, interfaces, attributes and assignability passed.");
    }
}
