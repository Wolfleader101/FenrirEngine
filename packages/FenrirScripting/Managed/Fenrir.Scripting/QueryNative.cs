using System.Runtime.InteropServices;

namespace Fenrir.Scripting;

/// <summary>Where queries start. The generated part adds <c>Query&lt;...&gt;()</c> for 1 to 8 components.</summary>
public static partial class World
{
}

// The scratch buffer a query reads its rows from. Two native calls fill it, however many entities there are: one for an
// upper bound on the rows and one that writes them. A row is the entity (an Entity as C++ lays it out, its value and
// its scene's list) followed by a pointer to each component.
internal static unsafe class QueryNative
{
    // The words a row spends on the entity: the size of the Entity mirror, which the engine checks against C++ when a
    // script assembly loads, in pointers. The generated query types index a row by it. (A struct's sizeof is not a C#
    // constant; the JIT folds a static readonly int once the method is tiered up.)
    internal static readonly int EntityWords = sizeof(Entity) / sizeof(nuint);

    // Fills a new buffer with every entity that has all the components. Null (and a count of zero) when there are none.
    // The caller owns it: give it back with Release.
    internal static nuint* Fill(int* ids, int count, out int rows)
    {
        for (int first = 0; first < count; first++)
            for (int second = first + 1; second < count; second++)
                if (ids[first] == ids[second])
                    throw new InvalidOperationException("A query names the same component twice.");

        rows = 0;
        var idSpan = new ReadOnlySpan<int>(ids, count);
        int capacity = Native.QueryCount(idSpan);
        if (capacity < 0)
            throw new InvalidOperationException("The engine does not know one of the components in this query.");
        if (capacity == 0)
            return null;

        int stride = count + EntityWords;
        var buffer = (nuint*)NativeMemory.Alloc((nuint)capacity * (nuint)stride, (nuint)sizeof(nuint));
        // QueryFill's capacity counts rows, not entries, so the span it takes is `capacity` long: the native gets the
        // same count it always did, and never writes past the buffer.
        rows = Native.QueryFill(idSpan, new Span<nuint>(buffer, capacity));
        if (rows < 0)
        {
            NativeMemory.Free(buffer);
            rows = 0;
            throw new InvalidOperationException("The engine could not run the query.");
        }
        return buffer;
    }

    internal static void Release(nuint* rows)
    {
        if (rows != null)
            NativeMemory.Free(rows);
    }
}
