using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using Iris.Bridge;

// The numbers the bridge gives to the things it shares with the native side, compared with the table both sides read
// (Tests/numbering.txt, found through IRIS_NUMBERING_FILE). The native side has the same check against the same table.
static class NumberingChecks
{
    // The table's groups, and the bridge's enum for each. The "NativeStatus" group is only the native side's.
    private static Dictionary<string, Type> Enums() => new()
    {
        ["ValueKind"] = typeof(ValueKind),
        ["TypeKind"] = NestedEnum(typeof(TypeQueries), "Kind"),
        ["Visibility"] = NestedEnum(typeof(TypeQueries), "Visibility"),
        ["Status"] = typeof(BridgeStatus),
        ["LogLevel"] = typeof(LogLevel),
        ["UnloadState"] = typeof(ComponentUnloadState)
    };

    private static Type NestedEnum(Type owner, string name) =>
        owner.GetNestedType(name, BindingFlags.NonPublic | BindingFlags.Public)
        ?? throw new Exception($"{owner.Name} has no enum named {name}");

    internal static void Run()
    {
        string path = Environment.GetEnvironmentVariable("IRIS_NUMBERING_FILE")
            ?? throw new Exception("IRIS_NUMBERING_FILE does not name the numbering table");
        var table = new Dictionary<string, int>();
        foreach (string line in File.ReadAllLines(path))
        {
            if (line.Length == 0 || line[0] == '#') continue;
            string[] parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            table[parts[0] + "." + parts[1]] = int.Parse(parts[2]);
        }

        var failures = new List<string>();
        var seen = new HashSet<string>();
        foreach ((string group, Type type) in Enums())
        {
            foreach (string name in Enum.GetNames(type))
            {
                string key = group + "." + name;
                int number = Convert.ToInt32(Enum.Parse(type, name));
                seen.Add(key);
                if (!table.TryGetValue(key, out int expected))
                    failures.Add($"{key} is not in the numbering table");
                else if (expected != number)
                    failures.Add($"{key}: the table says {expected} but the bridge has {number}");
            }
        }
        foreach ((string key, int number) in table)
        {
            if (key.StartsWith("NativeStatus.")) continue;
            if (!seen.Contains(key)) failures.Add($"{key} is in the numbering table (as {number}) but the bridge does not define it");
        }
        if (failures.Count > 0) throw new Exception(string.Join(Environment.NewLine, failures));
        Console.WriteLine($"Numbering: {seen.Count} items agree with the table shared with the native side.");
    }
}
