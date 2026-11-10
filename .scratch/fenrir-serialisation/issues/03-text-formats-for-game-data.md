# Which text formats suit scene files and data assets, and what libraries read them?

Type: research
Status: open
Blocked by: none

## Question

Compare the text formats a development-time scene file and data asset could use, as a format and as C++ and C# libraries, in the areas that constrain the one format Fenrir picks:

- **Candidates:** JSON (and JSON5 or JSONC for comments), TOML, YAML (Unity's flavour, with its known pitfalls), Godot's `.tscn`/`.tres` INI-like format, KDL, S-expressions, and the case for a small custom grammar.
- **For hand editing and review:** comments, trailing commas, multi-line strings, readability of nested lists of structs (a tuning table), float round-tripping (shortest representation, infinities, NaN), stable key ordering for byte-identical rewrites, and how diffs and three-way merges behave on each.
- **For the data model:** how each writes a typed reference (a path or id plus a type), polymorphic values (a derived type in a base-typed field), sub-objects embedded in a file, and binary blobs (base64, sidecar files).
- **Libraries, C++:** nlohmann/json, simdjson, RapidJSON, Glaze, toml++, yaml-cpp, rapidyaml, kdl-cpp; licence, size, build cost as a pinned `PRIVATE` submodule (ADR-0010's pattern), error reporting with line and column, and writing with preserved ordering and formatting.
- **Libraries, C#:** System.Text.Json, Tomlyn, YamlDotNet; whether the same file reads identically from both languages matters, or only C++ reads files and C# sees the loaded data.
- **Prior art:** which format each engine above chose and what they have said about regretting it (Unity's YAML merges, Godot's move from XML to its own format, Bevy's RON and BSN).

Write findings to a Markdown file on the `research/text-formats` branch (`.scratch/fenrir-serialisation/research/text-formats.md`), citing each claim's source and pinning the versions read, and link it here.
