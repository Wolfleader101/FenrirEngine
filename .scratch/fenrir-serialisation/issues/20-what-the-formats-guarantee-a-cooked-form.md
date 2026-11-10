# What must the development formats guarantee so a cooked shipping form can follow?

Type: grilling
Status: open
Blocked by: 11, 12, 13

## Question

A shipped game needs a binary or cooked form of its scenes and data assets, which this map does not specify. What must the development formats and the engine API promise now so that form can be added later without changing game code or the files?

- **Identity:** references that a cooker can replace with offsets or indices (as [How are assets identified?](11-asset-identity-and-references.md) decided), no absolute paths, and nothing resolved by scanning the asset root at runtime.
- **Descriptions without headers:** the type descriptions a cooked reader needs available without the C++ headers or the C# assemblies, or a cooked form laid out so none are needed.
- **One door:** whether game code loads through one API that a cooked build serves from a pack and a development build from text files, and whether the asset root's layout is part of the contract.
- **Embedded data:** which engine defaults are already embedded (built-in shaders) and whether the pattern extends to a game's cooked pack in the executable.
- **What is deferred:** the cooked format, the cooker, compression, platform packaging, and whether they are a later spec on this map or a new effort; this ticket's resolution updates the map's fog accordingly.

Zoom into [Which text format do files use?](12-which-text-format-and-versioning.md) and [What does a scene file hold?](13-what-a-scene-file-holds.md).
