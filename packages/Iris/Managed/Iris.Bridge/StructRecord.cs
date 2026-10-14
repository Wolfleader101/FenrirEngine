using System;

namespace Iris.Bridge;

// A struct or enum registered with a component: the managed type, its size in bytes, the offset of every bool byte
// in it (to be made 0 or 1 on the way in), and for an enum the wire kind of its underlying integer.
internal sealed record StructRecord(Type Type, int Size, int[] BoolOffsets, int UnderlyingKind = 0);
