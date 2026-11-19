# Format fit: TOML, JSON, JSON5 and YAML against Fenrir's settled needs

Research for [12 Which text format do files use, and how are they versioned and migrated?](../issues/12-which-text-format-and-versioning.md), zooming in from [text-formats.md](text-formats.md) (branch `research/text-formats`, the abstract comparison, not repeated here). Read and measured on 2026-11-19; every claim carries an `[F<n>]` pointer into the Sources section, which names the version, tag or commit read. Measurements were made on clones in a scratch directory and on sample files in a temp directory of this worktree, with git 2.47.0, Python 3.12 and Node 24 [F0].

The settled needs come from ADR-0060, ADR-0061, ADR-0062 and the map's Notes [F1]: a reference is a plain path string typed by its field; the only type names written are the top-level asset's and each embedded sub-asset's (plus a per-type version and a per-file format version); one top-level asset per file with `#name` sub-assets; scene files hold many entities with a parent, a stable per-file id and a handful of components; data assets hold nested lists of structs, strings and optionals; defaults are omitted, keys are written in declaration order, floats shortest round-trip, `Infinity`/`NaN` representable, the same data writes the same bytes; hand editing, comments surviving a rewrite, and diffs and merges of scene files matter; diagnostics carry file:line:column and a bad field takes its default; C++ is the reader of record and C# never parses files; the library is a `PRIVATE` pinned submodule or the engine owns its parser. The repo is C++20 on CMake 3.20 with twelve pinned submodules [F2].

## Summary

- **The four formats write the same two files in 23 to 30 lines (sword) and 46 to 65 lines (scene)**; YAML is the smallest (565 and 1,101 bytes), TOML next (577 and 1,172), JSON5 (688 and 1,451) and strict JSON (754 and 1,515) the largest, the brace formats costing about 40 % more lines than YAML on the scene [F0].
- **Diffs and merges came out identical across formats**: a float change is one line in each; appending an entity is a pure insertion in each (strict JSON included, because git aligns the identical closing brace); two people appending at the end of the file conflict in every format; the same two additions merge cleanly in every format once they land in different places. Layout and identity, not syntax, decide merges [F0].
- **An engine-owned JSON5 reader and writer is about 1,300 to 2,000 lines of C++.** The reference JavaScript parser plus stringifier is 1,575 lines, cj5 (C99, tokens only) 1,325, json5pp (C++11, parse and stringify) 2,169, P-i-N/json5's core 1,584; a minimal strict JSON parser and writer is 1,200 lines (picojson). nlohmann's lexer, parser, serializer and float printer alone are 4,268 lines inside a 25,368-line, 979 KB header set whose git history is 328 MB; rapidyaml is 27,720 lines plus 12,675 of vendored c4core; toml++ 21,806; yaml-cpp 15,965 [F3][F4][F5][F6][F7][F8][F9][F10].
- **Every library path still needs an engine-owned writer or writer rules** (toml++ sorts keys; nlohmann writes `null` for infinity; yaml-cpp and rapidyaml decide quoting and float text themselves), and **no library keeps comments through a rewrite**; only an engine-owned parser with trivia nodes does [F5][F6][F7][F8][F11].
- **Hand-editing hazards are real and format-specific**: YAML's ` #` turns an unquoted `#sparks` into a comment and a mis-indented component silently becomes a field of the entity, which a tolerant load then hides; TOML's `[entities.components."…"]` headers bind to the most recent `[[entities]]` so a moved block re-parents a component, and its inline tables are single-line in 1.0 but multi-line in 1.1 (toml++ 3.4.0 reads only 1.0); strict JSON cannot carry a comment or `Infinity`; JSON5 has no maintained C++ library and VS Code highlights it only through an extension (JetBrains, Vim and GitHub do so built in) [F12][F13][F14][F15][F16][F17][F18][F19][F20].
- **Researcher's recommendation: JSON5, read and written by an engine-owned parser (no submodule), emitting a fixed subset; runner-up YAML 1.2 block style through rapidyaml** (see the last section). Both are for the user to accept or reject.

## 1. The same two files in four formats

Conventions shared by all variants: a header of `format` (per-file format version), `type` (canonical type name, ADR-0060 §6) and `version` (per-type version); the asset's fields under `data`; embedded sub-assets under `sub` by name, each with its own `type`, `version` and `data`; references are plain path strings (`models/sword.glb#Mesh0`, `#sparks`) typed by the field (ADR-0062 §2); vectors and quaternions are arrays; fields equal to their default (`description`, `scale`, `castsShadows`) are omitted (ADR-0061 §7); keys are in declaration order. The scene has four entities written parent-first, each with a stable five-character id, a `parent` id and a `components` map keyed by type name; `Fenrir::Sky` is a scene resource keyed by type (one per type, `GLOSSARY.md`); `MyGame::BoneAttachment.skeleton` is an entity reference by per-file id. Strict JSON cannot hold a comment or `Infinity`, so its variants carry the comment as a `$comment` key (data the reader must skip) and the infinity as the string `"Infinity"`, the convention System.Text.Json uses [F21]; both are conventions the engine would own, not features of the format.

### (a) `weapons/sword`, a `MyGame::Sword` data asset

YAML (23 lines, 565 bytes):

```yaml
# Iron sword: tuning numbers; `description` is left out, so it takes the type's default
format: 1
type: MyGame::Sword
version: 2
data:
  displayName: Iron Sword
  damage: 12.5
  durability: .inf
  mesh: models/sword.glb#Mesh0
  tags: [melee, one-handed, iron]
  upgrades:
    - {level: 1, cost: 100, damageBonus: 1.5}
    - {level: 2, cost: 250, damageBonus: 3.25}
    - {level: 3, cost: 600, damageBonus: 6}
  onHit: "#sparks"
sub:
  sparks:
    type: MyGame::ParticleEffect
    version: 1
    data:
      texture: fx/spark.png
      count: 24
      lifetime: 0.4
```

TOML (26 lines, 577 bytes; TOML 1.0, inline tables kept on one line):

```toml
# Iron sword: tuning numbers; `description` is left out, so it takes the type's default
format = 1
type = "MyGame::Sword"
version = 2

[data]
displayName = "Iron Sword"
damage = 12.5
durability = inf
mesh = "models/sword.glb#Mesh0"
tags = ["melee", "one-handed", "iron"]
upgrades = [
  { level = 1, cost = 100, damageBonus = 1.5 },
  { level = 2, cost = 250, damageBonus = 3.25 },
  { level = 3, cost = 600, damageBonus = 6.0 },
]
onHit = "#sparks"

[sub.sparks]
type = "MyGame::ParticleEffect"
version = 1

[sub.sparks.data]
texture = "fx/spark.png"
count = 24
lifetime = 0.4
```

JSON5 (30 lines, 688 bytes; bare keys, double-quoted strings, trailing commas):

```json5
// Iron sword: tuning numbers; `description` is left out, so it takes the type's default
{
  format: 1,
  type: "MyGame::Sword",
  version: 2,
  data: {
    displayName: "Iron Sword",
    damage: 12.5,
    durability: Infinity,
    mesh: "models/sword.glb#Mesh0",
    tags: ["melee", "one-handed", "iron"],
    upgrades: [
      {level: 1, cost: 100, damageBonus: 1.5},
      {level: 2, cost: 250, damageBonus: 3.25},
      {level: 3, cost: 600, damageBonus: 6},
    ],
    onHit: "#sparks",
  },
  sub: {
    sparks: {
      type: "MyGame::ParticleEffect",
      version: 1,
      data: {
        texture: "fx/spark.png",
        count: 24,
        lifetime: 0.4,
      },
    },
  },
}
```

Strict JSON (30 lines, 754 bytes; `$comment` and `"Infinity"` are conventions):

```json
{
  "$comment": "Iron sword: tuning numbers; `description` is left out, so it takes the type's default",
  "format": 1,
  "type": "MyGame::Sword",
  "version": 2,
  "data": {
    "displayName": "Iron Sword",
    "damage": 12.5,
    "durability": "Infinity",
    "mesh": "models/sword.glb#Mesh0",
    "tags": ["melee", "one-handed", "iron"],
    "upgrades": [
      {"level": 1, "cost": 100, "damageBonus": 1.5},
      {"level": 2, "cost": 250, "damageBonus": 3.25},
      {"level": 3, "cost": 600, "damageBonus": 6}
    ],
    "onHit": "#sparks"
  },
  "sub": {
    "sparks": {
      "type": "MyGame::ParticleEffect",
      "version": 1,
      "data": {
        "texture": "fx/spark.png",
        "count": 24,
        "lifetime": 0.4
      }
    }
  }
}
```

### (b) a scene: `Level` > `Player` > `Sword`, `Level` > `Sun`, resource `Fenrir::Sky`

YAML (46 lines, 1,101 bytes):

```yaml
# Level 1: the player's start room; entities are written parent-first
format: 1
type: Fenrir::Scene
version: 1
resources:
  Fenrir::Sky:
    cubemap: textures/sky/day.ktx2
    exposure: 1.2
entities:
  - id: a7k2q
    components:
      Fenrir::Name:
        value: Level
  - id: d4e5f
    parent: a7k2q
    components:
      Fenrir::Name:
        value: Player
      Fenrir::Transform:
        position: [0, 1.8, 0]
        rotation: [0, 0.7071068, 0, 0.7071068]
      Fenrir::MeshRenderer:
        mesh: models/player.glb#Mesh0
        material: materials/player.mat
  - id: m9x1p
    parent: d4e5f
    components:
      Fenrir::Name:
        value: Sword
      Fenrir::MeshRenderer:
        mesh: models/sword.glb#Mesh0
        material: materials/iron.mat
      MyGame::BoneAttachment:
        skeleton: d4e5f
        bone: hand_r
  - id: s3w8c
    parent: a7k2q
    components:
      Fenrir::Name:
        value: Sun
      Fenrir::Transform:
        rotation: [-0.3826834, 0, 0, 0.9238795]
      Fenrir::DirectionalLight:
        color: [1, 0.95, 0.8]
        intensity: 3.5
        shadows: Soft
```

TOML (49 lines, 1,172 bytes; array of tables, one sub-table header per component, `::` keys quoted, floats written `0.0` because TOML types integers and floats apart):

```toml
# Level 1: the player's start room; entities are written parent-first
format = 1
type = "Fenrir::Scene"
version = 1

[resources."Fenrir::Sky"]
cubemap = "textures/sky/day.ktx2"
exposure = 1.2

[[entities]]
id = "a7k2q"
[entities.components."Fenrir::Name"]
value = "Level"

[[entities]]
id = "d4e5f"
parent = "a7k2q"
[entities.components."Fenrir::Name"]
value = "Player"
[entities.components."Fenrir::Transform"]
position = [0.0, 1.8, 0.0]
rotation = [0.0, 0.7071068, 0.0, 0.7071068]
[entities.components."Fenrir::MeshRenderer"]
mesh = "models/player.glb#Mesh0"
material = "materials/player.mat"

[[entities]]
id = "m9x1p"
parent = "d4e5f"
[entities.components."Fenrir::Name"]
value = "Sword"
[entities.components."Fenrir::MeshRenderer"]
mesh = "models/sword.glb#Mesh0"
material = "materials/iron.mat"
[entities.components."MyGame::BoneAttachment"]
skeleton = "d4e5f"
bone = "hand_r"

[[entities]]
id = "s3w8c"
parent = "a7k2q"
[entities.components."Fenrir::Name"]
value = "Sun"
[entities.components."Fenrir::Transform"]
rotation = [-0.3826834, 0.0, 0.0, 0.9238795]
[entities.components."Fenrir::DirectionalLight"]
color = [1.0, 0.95, 0.8]
intensity = 3.5
shadows = "Soft"
```

JSON5 (65 lines, 1,451 bytes):

```json5
// Level 1: the player's start room; entities are written parent-first
{
  format: 1,
  type: "Fenrir::Scene",
  version: 1,
  resources: {
    "Fenrir::Sky": {
      cubemap: "textures/sky/day.ktx2",
      exposure: 1.2,
    },
  },
  entities: [
    {
      id: "a7k2q",
      components: {
        "Fenrir::Name": {value: "Level"},
      },
    },
    {
      id: "d4e5f",
      parent: "a7k2q",
      components: {
        "Fenrir::Name": {value: "Player"},
        "Fenrir::Transform": {
          position: [0, 1.8, 0],
          rotation: [0, 0.7071068, 0, 0.7071068],
        },
        "Fenrir::MeshRenderer": {
          mesh: "models/player.glb#Mesh0",
          material: "materials/player.mat",
        },
      },
    },
    {
      id: "m9x1p",
      parent: "d4e5f",
      components: {
        "Fenrir::Name": {value: "Sword"},
        "Fenrir::MeshRenderer": {
          mesh: "models/sword.glb#Mesh0",
          material: "materials/iron.mat",
        },
        "MyGame::BoneAttachment": {
          skeleton: "d4e5f",
          bone: "hand_r",
        },
      },
    },
    {
      id: "s3w8c",
      parent: "a7k2q",
      components: {
        "Fenrir::Name": {value: "Sun"},
        "Fenrir::Transform": {
          rotation: [-0.3826834, 0, 0, 0.9238795],
        },
        "Fenrir::DirectionalLight": {
          color: [1, 0.95, 0.8],
          intensity: 3.5,
          shadows: "Soft",
        },
      },
    },
  ],
}
```

Strict JSON (65 lines, 1,515 bytes):

```json
{
  "$comment": "Level 1: the player's start room; entities are written parent-first",
  "format": 1,
  "type": "Fenrir::Scene",
  "version": 1,
  "resources": {
    "Fenrir::Sky": {
      "cubemap": "textures/sky/day.ktx2",
      "exposure": 1.2
    }
  },
  "entities": [
    {
      "id": "a7k2q",
      "components": {
        "Fenrir::Name": {"value": "Level"}
      }
    },
    {
      "id": "d4e5f",
      "parent": "a7k2q",
      "components": {
        "Fenrir::Name": {"value": "Player"},
        "Fenrir::Transform": {
          "position": [0, 1.8, 0],
          "rotation": [0, 0.7071068, 0, 0.7071068]
        },
        "Fenrir::MeshRenderer": {
          "mesh": "models/player.glb#Mesh0",
          "material": "materials/player.mat"
        }
      }
    },
    {
      "id": "m9x1p",
      "parent": "d4e5f",
      "components": {
        "Fenrir::Name": {"value": "Sword"},
        "Fenrir::MeshRenderer": {
          "mesh": "models/sword.glb#Mesh0",
          "material": "materials/iron.mat"
        },
        "MyGame::BoneAttachment": {
          "skeleton": "d4e5f",
          "bone": "hand_r"
        }
      }
    },
    {
      "id": "s3w8c",
      "parent": "a7k2q",
      "components": {
        "Fenrir::Name": {"value": "Sun"},
        "Fenrir::Transform": {
          "rotation": [-0.3826834, 0, 0, 0.9238795]
        },
        "Fenrir::DirectionalLight": {
          "color": [1, 0.95, 0.8],
          "intensity": 3.5,
          "shadows": "Soft"
        }
      }
    }
  ]
}
```

### Sizes (`wc -lc`, LF endings, trailing newline) [F0]

| file | YAML | TOML | JSON5 | JSON |
|---|---|---|---|---|
| sword, lines | 23 | 26 | 30 | 30 |
| sword, bytes | 565 | 577 | 688 | 754 |
| scene, lines | 46 | 49 | 65 | 65 |
| scene, bytes | 1,101 | 1,172 | 1,451 | 1,515 |

Each variant was parsed: YAML by inspection against the 1.2.2 core schema (no PyYAML on this machine), TOML by Python's `tomllib` (1.0), JSON by Python's `json`, JSON5 by the reference `json5/json5` implementation, which read `durability` as `Infinity` and four entities [F0][F3]. The brace formats spend their extra lines on closing `}`/`]` lines (18 of the scene's 65 lines in JSON5 are a lone bracket); TOML spends them on table headers (15 of 49: one resource table, four `[[entities]]`, ten component sub-tables).

### Diffs and merges of the scene (`git diff --no-index`, `git merge-file`) [F0]

Edits: (i) `Sun.intensity` 3.5 → 4.25; (ii) append a `Torch` entity (child of `Level`: `Name`, `Transform`, `PointLight`) at the end; (iii-a) person A appends `Torch` at the end, person B appends a `Camera` entity (child of `Player`: `Name`, `Transform`, `Camera`) at the end; (iii-b) A appends `Torch` at the end, B inserts `Camera` directly after `Sword` (the other child of `Player`), before `Sun`.

| format | (i) float | (ii) append | (iii-a) both append at end | (iii-b) additions under different parents |
|---|---|---|---|---|
| YAML | +1 −1 | +11 −0 | conflict, 22-line region | clean |
| TOML | +1 −1 | +12 −0 | conflict, 20-line region | clean |
| JSON5 | +1 −1 | +15 −0 | conflict, 22-line region | clean |
| JSON | +1 −1 | +15 −0 | conflict, 22-line region | clean |

The float change is one line in every format, for example TOML:

```diff
 color = [1.0, 0.95, 0.8]
-intensity = 3.5
+intensity = 4.25
 shadows = "Soft"
```

The append is a pure insertion in every format. In strict JSON the previous entity's closing `}` must gain a comma, but git aligns the new block's final `}` with the old one, so the hunk reads as `+    },` plus the block, still +15 −0; the inserted hunk merely starts one line earlier than JSON5's. The trailing-comma penalty the prior research expected [F11] did not appear for multi-line objects; it does appear for a single-line list such as `tags: ["melee", "one-handed", "iron"]`, where any append rewrites that line in all four formats alike.

Both people appending at the end of the file conflicts in every format, because git's `text` driver sees two different insertions at the same position [F22]; the conflict region is the two blocks (20 to 22 lines). The same two entities merge cleanly in every format when they are inserted at different places, here because the writer puts children directly after their parent, so a new child of `Player` and a new child of `Level` land in different hunks separated by `Sun`'s unchanged block. Hunks of about ten unchanged lines between them were enough; two insertions with no unchanged line between them would conflict in any format.

What the measurement says: syntax did not separate the four. Merge outcomes come from the layout rule (one entity per block, children after their parent, so concurrent additions rarely touch the same spot), stable ids that are never renumbered on save (Godot's reason for pseudo-random readable ids [F11]) and a writer that never reorders or reformats what it did not change.

## 2. Own-parser cost against library cost

### Measured line counts [F3][F4][F5][F6][F7][F8][F9][F10]

| implementation | language, scope | lines | commit or tag read |
|---|---|---|---|
| json5/json5 `lib/parse.js` + `stringify.js` + `util.js` + `unicode.js` + `index.js` | JavaScript, reference JSON5 parser and stringifier | 1,190 + 333 + 48 + 4 + 5 = **1,580** | 6d21f2f (v2.2.3+39), 2026-10-29 |
| septag/cj5 `cj5.h` | C99 single header, JSON5 tokeniser (jsmn-style token array, no tree, no writer) | **1,325** | 9440a95, 2026-11-02, MIT |
| kimushu/json5pp `json5pp.hpp` | C++11 single header, JSON and JSON5 parse and stringify with a value type | **2,169** | 559202e, 2020-11-16 |
| P-i-N/json5 `include/json5/*.hpp` | C++20 headers: base 253, input 600, output 232, document 499 (= 1,584 core), reflection 515, filter 109, builder 171 | **2,416** (1,584 core) | 11084bf, 2021-05-03, MIT |
| kazuho/picojson `picojson.h` | C++ single header, strict JSON parse and serialise | **1,200** | 111c9be, 2021-01-18 |
| DaveGamble/cJSON `cJSON.c` + `.h` | C, strict JSON parse and print | **3,512** | 6d9f244, 2026-09-16 |
| nlohmann/json core: `lexer.hpp` 1,643, `parser.hpp` 519, `serializer.hpp` 988, `to_chars.hpp` 1,118 | C++11, strict JSON (plus comment skipping) | **4,268** (+ `json_sax.hpp` 986, input/output adapters 696 = 5,950) | v3.12.0, 2025-04-11 |
| nlohmann/json whole `include/` | 46 headers | **25,368** (single header 25,526 lines, 978,962 bytes) | v3.12.0 |
| rapidyaml `src/c4/yml` | C++11, YAML 1.2 parser, tree, emitter (`parse_engine.def.hpp` alone 8,922) | **27,720** in 58 files | v0.16.0, 2026-07-21 |
| rapidyaml vendored c4core `ext/c4core.src/c4` | C++11 substr, charconv, memory | **12,675** in 42 files | v0.16.0 |
| toml++ `include/toml++` | C++17, TOML 1.0 parser, tree, formatters (`parser.inl` 3,917, formatters 928) | **21,806** in 52 files (single `toml.hpp` 17,748 lines, 485,931 bytes) | v3.4.0, 2023-10-13 |
| yaml-cpp `src` + `include` | C++11, YAML 1.2 scanner/parser (~1,664 lines), emitter (~1,876), node tree | **12,019 + 3,946 = 15,965** in 93 files | yaml-cpp-0.9.0, 2026-02-04 |

The prior note's "1.9 MB single header" for nlohmann [F11] does not match the v3.12.0 tag as measured here (979 KB); the multi-header tree is 25,368 lines either way.

### Estimate for a Fenrir-owned JSON5 reader and writer

An honest line estimate, by part, calibrated against the implementations above (the reference parser handles the full grammar in 1,190 lines of JavaScript; json5pp parses and stringifies in 2,169 lines of C++11 including its value type):

| part | lines (C++20, excluding tests) | notes |
|---|---|---|
| lexer: punctuation, both quote styles with escapes, `\u` and line continuations, numbers with hex, leading or trailing dot, sign, `Infinity`, `NaN`, identifier keys, `//` and `/* */` comments, JSON5 whitespace set | 450–650 | the bulk of every JSON5 implementation [F3][F4] |
| parser to a tree, with recursion depth limit | 150–250 | |
| document tree: ordered object (vector of key/value), array, string, number (double plus an "is integer literal" flag), bool, null; `line:column` per node | 250–400 | ordered object is what gives declaration-order rewrites for free |
| trivia: a comment attaches as leading trivia to the next key or element, or as trailing trivia to its container; blank lines optional | 150–300 | the one feature no library offers [F11] |
| writer: 2-space indent, trailing commas, bare key when the key is an identifier else quoted, one escaping rule, `std::to_chars` shortest floats, `Infinity`/`NaN` literals, flow style for arrays of scalars and single-field objects, block style otherwise | 200–300 | deterministic by construction; the only decisions are these rules |
| diagnostics: file, line, column, expected/found | 50–100 | |
| **total** | **1,250–2,000** | plus a comparable volume of tests and the JSON5 test suite as fixtures |

Against that, the type-driven reader that walks a tree with the registry (ADR-0060's visitor), applies defaults and reports a bad field is the same cost whichever parser produces the tree, and is not counted here or for the libraries.

### What each library path still leaves the engine to write

| path | submodule | engine still writes | comments survive a rewrite |
|---|---|---|---|
| **own JSON5** | none | everything above (1,250–2,000 lines) | yes, through trivia nodes |
| **nlohmann (strict JSON) + hand-written writer** | nlohmann/json | a writer (~250 lines: `ordered_json` keeps insertion order but `dump()` writes `null` for non-finite floats [F5] and formats with Grisu2, so a deterministic emit with the `"Infinity"` convention needs its own); a reader-side convention for `"Infinity"` strings in float fields; `$comment` skipping; errors give a byte offset with line and column only in message text [F11] | no |
| **rapidyaml** | biojppm/rapidyaml (c4core vendored) | emit rules (tree order is preserved; float text is c4core's `to_chars` with the `FTOA_FLEX` precision caveat [F11]; quoting of `#`-bearing scalars must be forced per node); errors carry `Location{offset, line, col}` [F11] | no (no comment node type [F11]) |
| **yaml-cpp** | jbeder/yaml-cpp | emitter driving (the `Emitter` is called key by key so order is the engine's; Dragonbox shortest floats and `.inf` built in [F11]); `convert<bool>` leniency must be bypassed (below) | no (issues #154, #457 [F11]) |
| **toml++** | marzer/tomlplusplus | a writer (~300 lines: `toml::table` is a `std::map` sorted by key [F11], and the choice between `[[array]]` headers and inline tables is a formatting decision the library's formatter makes its own way); a 1.0-only reader until a post-3.4.0 release ships 1.1 [F11] | no (issue #28 "not planned" [F11]) |

### Library candidates as pinned submodules [F5][F6][F7][F8][F11]

| library | insertion/declaration order on write | keeps comments | line:column | reads / writes Infinity, NaN | licence | min C++ | clone size (`.git` + checkout) and tracked files | last release |
|---|---|---|---|---|---|---|---|---|
| nlohmann/json | `ordered_json` yes; default `json` sorts | no (skipped as whitespace with `ignore_comments`) | byte offset structured; line and column only in `what()` text | no / writes `null` (serializer checks `std::isfinite` and emits `null`); `1e999` parses to `inf` through `strtod` with no `ERANGE` check in the float path, but writes back as `null` | MIT | C++11 | **328 MB** `.git` + 27 MB checkout; 111 headers, 524 sources tracked (tests), 46 headers in `include/` | v3.12.0, 2025-04-11 |
| rapidyaml | tree order | no | yes, `Location{offset, line, col}`; opt-in per-node locations | `.inf`, `.nan` read and written | MIT | C++11 | 16 MB `.git` + 10 MB; 117 headers, 127 sources (c4core vendored since 0.16.0) | v0.16.0, 2026-07-21 |
| yaml-cpp | insertion order (`node_map` vector); `Emitter` in call order | no | yes, `Mark{pos, line, column}` | `.inf`, `.nan` | MIT | C++11 | 7 MB `.git` + 6 MB; 113 headers, 158 sources | 0.9.0, 2026-02-04 |
| toml++ | **no**, sorted `std::map` | no | yes, `source_region{begin{line, column}}` | `inf`, `nan` native | MIT | C++17 | 30 MB `.git` + 5 MB; 60 headers, 40 sources | v3.4.0, 2023-10-13 (TOML 1.0; master reads 1.1) |

nlohmann's history is the outlier: a default (non-shallow) submodule clone fetches 328 MB for a header-only library whose used part is 979 KB, because the single header is committed on every release; the repo's `.gitmodules` sets no `shallow = true` today [F2]. Glaze is excluded as before (C++23, CMake 3.31, VS 2026) [F11].

## 3. Hand-editing hazards, concretely

**YAML.**
- ` #` starts a comment inside a plain scalar: "Plain scalars must never contain the ': ' and ' #' character combinations" (1.2.2 §7.3.3) [F12]. `onHit: #sparks` is therefore `onHit: null` plus a comment; the writer must quote every string starting with `#` (the samples do), and a hand edit that drops the quotes loses the reference silently under a tolerant load.
- Indentation is structure: "tab characters must not be used in indentation" (§6.1) [F12], and a component dedented by two spaces (`Fenrir::Transform:` at the `components:` level) becomes a key of the entity map; a tolerant reader can at best warn "unknown field `Fenrir::Transform` on entity d4e5f", at worst drop it. The brace formats cannot mis-nest by whitespace.
- Booleans: the 1.2.2 core schema's bool regex is `true | True | TRUE | false | False | FALSE` [F12]. rapidyaml's c4core `from_chars(csubstr, bool*)` accepts exactly `0`, `1`, those six spellings and, as a fallback, any integer [F13], so `no` fails to parse as a bool (good) and `2` reads as `true` (a quirk). yaml-cpp's `convert<bool>::decode` accepts `y/n`, `yes/no`, `true/false`, `on/off` in lower, Capitalised or UPPER case [F14], so a `bool` field reads `no` as `false` and a string field reads `no` as the string, and the two libraries disagree; an engine that uses yaml-cpp would route bools through its own check to stay on 1.2.
- A `version: 1.0` or `bone: 010` typed without quotes is a float or an int to the parser; whether it reaches a string field depends on the engine reading the scalar text rather than the resolved type (both libraries expose the text).

**TOML.**
- Lists of structs: 1.0 says "Inline tables are intended to appear on a single line. A terminating comma … is not permitted … No newlines are allowed between the curly braces unless they are valid within a value" [F15]; 1.1.0 says "Inline tables can have multiple key/value pairs on the same line, or they can be put on different lines. A terminating comma … is permitted" [F16]. A tuning row wider than a screen is either one long line (1.0) or a `[[data.upgrades]]` header per row; a file written with 1.1's multi-line inline tables is a parse error in toml++ 3.4.0 [F11]. The sample keeps rows on one line.
- Position-dependent nesting: "Any reference to an array of tables points to the most recently defined table element of the array" [F15], so `[entities.components."Fenrir::Transform"]` belongs to whichever `[[entities]]` header last appeared above it. Cutting a component block and pasting it two entities down re-parents it with no error; a mistyped header `[entities.component."Fenrir::Transform"]` creates a new table silently (the tolerant reader sees an unknown field `component`).
- Canonical names as keys must be quoted (`"Fenrir::Transform"`); as values they are plain strings.
- Integers and floats are distinct types: `damageBonus = 6` is an integer node. toml++ converts an integer node to `double` through `value<double>()` only when it fits 2⁵³ [F17], so reading works, but a deterministic writer must choose one spelling (`6.0`) or a rewrite changes the file.
- "Key/value pairs within tables are not guaranteed to be in any specific order" [F15] and toml++ stores tables in a `std::map` [F11], so declaration order comes only from an engine-owned writer.

**Strict JSON.**
- No comments (RFC 8259 [F11]); the `$comment` key is data the reader must skip and that shows in every diff of the header.
- No trailing commas: a hand edit that leaves one is a parse error for the whole file; appending to a one-line list rewrites the line (true of every format), appending a multi-line object edits the previous closing brace (git still shows a pure insertion, section 1).
- No `Infinity`/`NaN`: "Numeric values that cannot be represented in the grammar below (such as Infinity and NaN) are not permitted" [F11]. The choices are the string convention (System.Text.Json's `"Infinity"` under `AllowNamedFloatingPointLiterals`, which its docs warn "can potentially produce JSON that cannot be parsed by other JSON implementations" [F21]) or the literal `1e999`, which is in the grammar, overflows to `inf` in nlohmann's `strtod` path [F5] and in Python's `json` [F0], but is not portable and is written back as `null` by nlohmann.
- Every key and string quoted: the sword costs 754 bytes against JSON5's 688 for the same content.

**JSON5.**
- Library support: no maintained C++ library was found. json5pp's last commit is 2020-11-16 and P-i-N/json5's 2021-05-03 [F4]; cj5 is active (2026-11-02) but is a C99 tokeniser without a tree or writer [F4]; meojson advertises JSON5 reading but no separate JSON5 header was found in its `include/` [F4]. The reference implementation is JavaScript [F3]. The spec's own site says JSON5 "is not intended to be used for machine-to-machine communication" [F18]. In practice JSON5 in Fenrir means the engine-owned parser of section 2, which is also what buys comment preservation.
- Editor tooling: VS Code's built-in `json` extension contributes the languages `json`, `jsonc`, `jsonl` and `snippets`, none claiming `.json5` [F19]; the `mrmlnc.vscode-json5` extension (266,109 installs) adds highlighting only [F19]. JetBrains IDEA and Rider recognise JSON5 and treat `.json5` files as JSON5 by default [F20]. Vim ships `runtime/syntax/json5.vim` (2019) and maps the `json5` extension in `autoload/dist/ft.vim` [F20]. GitHub's linguist has a `JSON5` language with `.json5`, highlighted through the JavaScript grammar [F20]. VS Code likewise has no built-in TOML language (no `toml` folder among the built-in extensions [F19]), so TOML also needs an extension there; YAML and JSON are built in everywhere.
- Superset discipline: every strict JSON file is valid JSON5, but a JSON5 file with bare keys, trailing commas or `Infinity` is unreadable by `jq`, nlohmann and System.Text.Json. If the writer is told to quote keys, drop trailing commas and never emit non-finite floats, its output is strict JSON; the engine could offer that switch for export without changing the reader.

**All four.** A syntax error anywhere fails the whole parse in every format and library (tolerance "a bad field takes its default" is a semantic rule applied to a parsed tree, not a recovery from broken syntax). Duplicate keys are "unpredictable" in JSON and JSON5, forbidden in YAML and TOML [F11]; the engine's tree should refuse them with a line and column in all cases.

## 4. Merge tooling

- **Unity's model.** `UnityYAMLMerge` is wired as a `git mergetool` (`[mergetool "unityyamlmerge"] cmd = '<path>' merge -p "$BASE" "$REMOTE" "$LOCAL" "$MERGED"`, `trustExitCode = false`), not as a `.gitattributes` merge driver, and merges `.unity`, `.prefab` and `.meta` semantically; other YAML needs `--force`. `mergerules.txt` has `[arrays]` rules that declare an array `set` (elements matched by named key fields, e.g. `set *.GameObject.m_Component *.fileID`) or `plain` (by position), `[exclusions]`, and `[comparisons]` with per-path float tolerances (`float *.Transform.m_LocalPosition.x 0.0000005`); unresolved conflicts go to a fallback tool from `mergespecfile.txt`, and exit codes 2 and 3 mean conflicts (3: Unity type rules broken) [F23]. The reusable idea is the rule shape: arrays of records keyed by an id field merge as sets; floats compare with tolerance; everything else is a tree 3-way merge. The tool itself is Unity-YAML-specific and closed.
- **Godot** ships no merge tool or driver; its docs say the format "aims to be VCS-friendly and generate mostly readable and mergeable files" and put only binary resources under LFS [F24]. Its contribution is layout: pseudo-random readable ids seeded deterministically so rewrites do not renumber [F11].
- **Generic drivers.** `git-json-merge` (npm, MIT) is a `merge.json.driver = git-json-merge %A %O %B` driver that resolves strict-JSON conflicts with xdiff and detects indentation; it would reject JSON5 syntax [F25]. Suture (`suture-driver` 5.1.0, AGPL-3.0 plus a commercial licence) claims semantic merging for JSON, YAML and TOML and installs itself with a script; its documentation does not state how array elements are matched or whether comments and key order survive, so it was not evaluated further [F25]. Nothing reusable for Fenrir's files was found in any format.
- **Cheaply written.** Given an engine-owned tree with locations (any of the paths in section 2 produces one), a Fenrir merge driver is a 3-way merge keyed by `entities[].id`, then by component type name, then by field: adds on both sides are kept, edits to different fields merge, edits to the same field conflict, with Unity's float tolerance as an option. That is on the order of 300–500 lines plus the writer the engine already owns, wired as `merge.fenrir.driver = fenrir-merge %O %A %B` with `*.scene.json5 merge=fenrir` in `.gitattributes`; git calls the driver only for files whose line merge conflicts [F22]. The cost is the same in all four formats; it only requires that the engine, not a library, owns the writer, so the merged output is byte-identical to what a save would produce.
- **Layout rule that makes plain line merges succeed** (measured in section 1): one entity per block with `id` as its first line; children written directly after their parent, depth first, so concurrent additions under different parents land in different hunks; the writer never reorders, renumbers or reformats blocks it did not change; ids are random readable strings minted once (so two people never mint the same next id). The residual conflict is two additions under the same parent, which only a semantic driver removes.

## 5. Scored table

Cells: **fits**, *partial* or **fails**, each with its reason. "Writer-owned" means the property comes from the engine's writer in every format; the cell then scores the format's own obstacles.

| need | TOML (toml++ 3.4.0 or own writer) | JSON strict (nlohmann 3.12.0 + own writer) | JSON5 (own reader and writer) | YAML 1.2 block (rapidyaml 0.16.0 or yaml-cpp 0.9.0) |
|---|---|---|---|---|
| plain path-string references, `#name` locally | **fits**: strings | **fits**: strings | **fits**: strings | *partial*: `#…` must be quoted or it is a comment [F12] |
| type names only on the top-level asset and sub-assets, plus two versions | **fits**: three keys in the root table | **fits** | **fits** | **fits** |
| scene with many entities, each a handful of components | *partial*: array of tables with one header per component, binding to the most recent `[[entities]]` [F15]; 49 lines | **fits** verbosely: 65 lines, 18 of them lone brackets | **fits** verbosely: 65 lines | **fits**: 46 lines, one fact per line |
| data assets with nested lists of structs, strings, optionals | *partial*: inline-table rows single-line in 1.0, multi-line only in 1.1 [F15][F16]; optionals omitted fine | **fits**: one row per line | **fits**: one row per line, trailing commas | **fits**: `- {…}` flow rows |
| omit defaults, declaration order | writer-owned; *partial* because toml++'s own formatter sorts [F11] | writer-owned; **fits** with `ordered_json` or own writer | writer-owned; **fits** | writer-owned; **fits** (both libraries keep order) |
| shortest round-trip floats | *partial*: int and float are distinct types, so the writer must spell `6.0` [F17] | **fits** (own writer) | **fits** (`std::to_chars`) | **fits** (Dragonbox in yaml-cpp; `to_chars` with a precision caveat in rapidyaml [F11]) |
| `Infinity`/`NaN` representable | **fits**: `inf`, `nan` [F15] | **fails**: not in the grammar; a string or `1e999` convention [F11][F21] | **fits**: literals [F18] | **fits**: `.inf`, `.nan` [F12] |
| same data, same bytes | *partial*: the table-vs-inline choice and quoting are rules the engine must fix | **fits** with own writer | **fits**: writer is the engine's | *partial*: quoting decisions are the library's unless forced per node |
| hand editing | **fits** for flat tables, *partial* for the nested scene (repeated headers, position-dependent) | *partial*: all quoted, no comments, no trailing commas | **fits**: comments, bare keys, trailing commas | **fits** with hazards: indentation, ` #`, bare `no` under yaml-cpp [F12][F14] |
| comments survive a tool rewrite | **fails** with toml++ (#28 "not planned") [F11]; fits only with an own parser | **fails**: no comments | **fits**: trivia nodes in the own parser | **fails** with both libraries [F11] |
| diffs and 3-way merges of scenes | **fits**: measured equal | **fits**: measured equal (insertion starts one line earlier) | **fits**: measured equal | **fits**: measured equal |
| diagnostics with file:line:column | **fits**: `source_region` [F11] | *partial*: offset structured, line and column only in text [F11] | **fits**: own | **fits**: `Location` / `Mark` [F11] |
| tolerant load (bad field takes its default) | *partial*: a mistyped or moved header silently re-homes data [F15] | **fits**: braces make structure explicit | **fits**: braces make structure explicit | *partial*: a dedented block silently becomes a sibling field [F12] |
| C++ reader of record as a PRIVATE submodule or own parser | *partial*: toml++ last released 2023-10, 1.0 only, 30 MB clone [F7] | **fits** as a submodule, at 328 MB of history for 979 KB of header [F5] | **fits** with no submodule at 1,250–2,000 lines of engine code (section 2) | **fits**: rapidyaml 16 MB or yaml-cpp 7 MB clone, compiled static libraries [F6][F8] |
| editor tooling for the chosen extension | *partial*: VS Code needs an extension [F19]; Vim and JetBrains built in | **fits** everywhere | *partial*: JetBrains, Vim and GitHub built in; VS Code needs a highlight-only extension [F19][F20] | **fits** everywhere |

Tally (fits / partial / fails): TOML 5 / 9 / 1; JSON 9 / 3 / 3; JSON5 13 / 1 / 0; YAML 10 / 4 / 1.

## 6. Researcher's recommendation (for the user to accept or reject)

**Recommendation: JSON5, read and written by an engine-owned parser and writer, with no third-party submodule.** Reasons, in order of weight:

1. **It is the only column with no fail and one partial**, and the partial (VS Code highlighting through an extension) is the cheapest to live with; JetBrains, Vim and GitHub highlight `.json5` out of the box [F19][F20]. The two needs the user named explicitly, hand editing and comments surviving a rewrite, are met only by this path, because comment preservation is a property of the writer and no library writer keeps them [F11]; an engine-owned tree with trivia nodes does.
2. **The own-parser cost is small and bounded**: 1,250–2,000 lines of C++ (section 2), calibrated against five implementations of 1,200–2,400 lines, against submodules of 16,000–40,000 lines that still need an engine-owned writer for declaration order, `Infinity` and deterministic bytes. JSON5's grammar is the smallest of the four (YAML's parser alone is 8,922 lines in rapidyaml), so it is the one format where owning the parser is cheaper than owning the writer rules around a library.
3. **Braces, not whitespace, carry structure**, so the hazards that interact badly with tolerant loading (YAML's dedent, TOML's header binding) do not exist; a bad field is a diagnostic at a line and column, never a silent re-home.
4. **Diffs and merges are not worse than any other format** (measured equal), and an engine-owned writer is exactly what a later semantic merge driver needs (section 4).
5. **The strict-JSON subset is one writer switch away** (quoted keys, no trailing commas, no non-finite floats), so export to `jq`, System.Text.Json or any JSON tool is available without a second reader, and every strict JSON file already reads.

Conditions that come with it: the writer emits a fixed subset (bare identifier keys, double-quoted strings, 2-space indent, trailing commas, flow style only for arrays of scalars and single-field objects, `std::to_chars` floats, `Infinity`/`NaN` literals); the reader accepts all of JSON5 and refuses duplicate keys; files use a double extension so editors key on `.json5` (`sword.asset.json5`, `level1.scene.json5`); the JSON5 test suite becomes fixtures. The risk accepted is that JSON5 was not designed for machine exchange [F18] and has no C++ ecosystem, so the parser's correctness rests on Fenrir's tests alone.

**Runner-up: YAML 1.2 core schema, block style, through rapidyaml** (MIT, C++11, 16 MB clone, structured `line:col`, strict 1.2 booleans [F6][F13]). It wins on size (the smallest files, 46 lines for the scene) and on readability of nested lists of structs, and it needs no engine parser. It loses on the two explicit needs: comments die on every rewrite, and ` #`, indentation and (under yaml-cpp) `yes`/`no` make hand edits fail quietly under tolerant loading [F12][F14]. Choose it if owning a parser is judged out of scope for this effort; then write comments into a `description` field as data.

**Why not the others.** TOML nests the scene as repeated position-bound headers, split into 1.0 and 1.1 readers in 2025-12, and toml++ (last release 2023-10) sorts keys, so the engine would own a writer anyway and still lose comments [F11][F15][F16]. Strict JSON's one edge over JSON5, readers everywhere including .NET's in-box one, is worth little because C# never parses files (map Notes [F1]); it cannot carry a comment or `Infinity` without a convention, and nlohmann's submodule is 328 MB of history for 979 KB of header [F5].

## Sources

- [F0] Measurements in this worktree's temp directory `.tmp-format-fit/` (not committed): the eight sample files above, `wc -lc`, `git diff --no-index --numstat` and `-U1`, `git merge-file -p` (git 2.47.0.windows.2), Python 3.12.12 `json`/`tomllib`, Node v24.21.0 running the reference `json5/json5` from source. Variants generated by a 200-line Python script that inserts the `Torch`/`Camera` blocks at the file end or before the `Sun` block.
- [F1] This repo at 66fddd36: `docs/adr/0060-*.md`, `0061-*.md`, `0062-*.md`; `.scratch/fenrir-serialisation/map.md` (Notes, Decisions so far); `.scratch/fenrir-serialisation/issues/12-which-text-format-and-versioning.md`.
- [F2] This repo at 66fddd36: `CMakeLists.txt` (`cmake_minimum_required(VERSION 3.20)`, `CMAKE_CXX_STANDARD 20`), `.gitmodules` (12 submodules, no `shallow` key).
- [F3] json5/json5, commit 6d21f2f86f58379171dbbadc5b00d0ae17ab9919 (2026-10-29, `git describe` v2.2.3-39), `lib/parse.js` 1,190 lines, `lib/stringify.js` 333, `lib/util.js` 48, `lib/unicode.js` 4, `lib/index.js` 5, `lib/cli.js` 157; MIT (`LICENSE.md`). https://github.com/json5/json5
- [F4] Small JSON5 and JSON implementations, line counts by `wc -l` on `--depth 1` clones: septag/cj5 `cj5.h` 1,325 (9440a95, 2026-11-02, MIT header) https://github.com/septag/cj5 ; kimushu/json5pp `json5pp.hpp` 2,169 (559202e, 2020-11-16; README "written in C++11") https://github.com/kimushu/json5pp ; P-i-N/json5 `include/json5/` 2,416 total (11084bf, 2021-05-03; README "small header only C++ library", reflection "requires C++20", MIT) https://github.com/P-i-N/json5 ; kazuho/picojson `picojson.h` 1,200 (111c9be, 2021-01-18) https://github.com/kazuho/picojson ; DaveGamble/cJSON `cJSON.c` 3,206 + `cJSON.h` 306 (6d9f244, 2026-09-16) https://github.com/DaveGamble/cJSON ; MistEO/meojson (91f6a7f, 2026-07-04): `include/` holds `json.hpp`, `common/`, `parser/`, `reflection/`, no file matching `*json5*` https://github.com/MistEO/meojson . Web searches on 2026-11-19 for "JSON5 parser C++ github single header" and "JSON5 parser C library github" surfaced these plus qvecs/qjson5, railgunlabs/judo and Daskie/qc-json (deprecated), none examined further.
- [F5] nlohmann/json at tag v3.12.0 (2025-04-11; newest tag by date): `include/nlohmann/detail/input/lexer.hpp` 1,643 lines (`strtof(double&…)` calls `std::strtod` with no `ERANGE` check; `errno`/`ERANGE` checked only in the integer branches), `parser.hpp` 519, `json_sax.hpp` 986, `input_adapters.hpp` 549, `output/serializer.hpp` 988 (`if (!std::isfinite(x)) { o->write_characters("null", 4); return; }`), `output_adapters.hpp` 147, `conversions/to_chars.hpp` 1,118, `json.hpp` 5,306; `include/` 46 files 25,368 lines; `single_include/nlohmann/json.hpp` 25,526 lines 978,962 bytes; clone `.git` 328 MB, checkout 27 MB, 1,677 tracked files (111 `.h/.hpp`, 524 `.c/.cpp`). https://github.com/nlohmann/json
- [F6] biojppm/rapidyaml at tag v0.16.0 (2026-07-21; HEAD 8608db91 2026-11-18): `src/c4/yml` 58 files 27,720 lines (`parse_engine.def.hpp` 8,922, `tree.hpp` 1,923, `tree.cpp` 2,021, `node.hpp` 2,151); vendored `ext/c4core.src/c4` 42 files 12,675 lines; clone `.git` 16 MB, checkout 10 MB, 463 tracked files (117 headers, 127 sources). https://github.com/biojppm/rapidyaml
- [F7] marzer/tomlplusplus at tag v3.4.0 (2023-10-13; HEAD c38c9b9 2026-11-15): `include/toml++` 52 files 21,806 lines (`impl/parser.inl` 3,917, `formatter.inl` 523, `toml_formatter.inl` 405, `table.hpp` 1,996, `node.hpp` 1,115, `value.hpp` 1,270); `toml.hpp` 17,748 lines 485,931 bytes; `CMakeLists.txt` `cxx_std_17`; clone `.git` 30 MB, checkout 5 MB, 212 tracked files (60 headers, 40 sources). https://github.com/marzer/tomlplusplus
- [F8] jbeder/yaml-cpp at tag yaml-cpp-0.9.0 (2026-02-04; HEAD 56e3bb5): `src` 55 files 12,019 lines (`scanner.cpp` 415, `scantoken.cpp` 444, `scanscalar.cpp` 251, `singledocparser.cpp` 434, `parser.cpp` 120, `emitter.cpp` 977, `emitterutils.cpp` 499, `emitterstate.cpp` 400, `fptostring.cpp` 255, `convert.cpp` 74); `include` 38 files 3,946 lines; `CMakeLists.txt` `CXX_STANDARD 11` default; clone `.git` 7 MB, checkout 6 MB, 394 tracked files. https://github.com/jbeder/yaml-cpp
- [F9] Release dates from `git tag --sort=-creatordate`: nlohmann v3.12.0 2025-04-11, v3.11.3 2023-11-28; rapidyaml v0.16.0 2026-07-21, v0.15.2 2026-06-08; toml++ v3.4.0 2023-10-13, v3.3.0 2023-01-29; yaml-cpp 0.9.0 2026-02-04, 0.8.0 2023-08-10.
- [F10] Estimate method: parts sized against [F3] and [F4] (the reference parser's 1,190 lines cover the full JSON5 grammar; json5pp's 2,169 lines include a value type and stringifier in C++11; P-i-N's input 600 + output 232 + base 253 + document 499 = 1,584 core lines).
- [F11] Prior findings, `.scratch/fenrir-serialisation/research/text-formats.md` on `research/text-formats` (commit a0e8d23b, read 2026-11-10): comment preservation per library (toml++ #28, yaml-cpp #154/#457, rapidyaml node type), key order (`ordered_json`, toml++ `std::map`), float printers (Grisu2, Dragonbox, `to_chars`/`FTOA_FLEX`), error structures (`parse_error`, `source_region`, `Mark`, `Location`), RFC 8259 §6 on Infinity/NaN and §4 on duplicate keys, Godot's pseudo-random ids, Glaze's toolchain, the "two-line diff" expectation for JSON appends, and the "1.9 MB single header" figure.
- [F12] YAML Ain't Markup Language 1.2.2 (2021-10-01): §6.1 "tab characters must not be used in indentation, since different systems treat tabs differently"; §6.6 "Comments must be separated from other tokens by white space characters"; §7.3.3 "Plain scalars must never contain the ': ' and ' #' character combinations"; §10.2.1.2 core schema bool `true | True | TRUE | false | False | FALSE`; §10.2.1.4 float `.inf`/`.nan`. https://yaml.org/spec/1.2.2/
- [F13] rapidyaml v0.16.0 `ext/c4core.src/c4/charconv.hpp`, `from_chars(csubstr buf, bool *v)`: accepts `0`, `1`, `true`/`True`/`TRUE`, `false`/`False`/`FALSE`, then "fallback to c-style int bools"; `src/c4/yml/scalar_charconv.hpp` and `parse_engine.def.hpp` match only `true`/`false`.
- [F14] yaml-cpp 0.9.0 `src/convert.cpp`, `convert<bool>::decode`: names table `{"y","n"}, {"yes","no"}, {"true","false"}, {"on","off"}` with `IsFlexibleCase` (all lower, Capitalised or ALL UPPER), comment "taken from http://yaml.org/type/bool.html".
- [F15] TOML v1.0.0 `toml.md` (toml-lang/toml tag 1.0.0): "Inline tables are intended to appear on a single line. A terminating comma (also called trailing comma) is not permitted after the last key/value pair in an inline table. No newlines are allowed between the curly braces unless they are valid within a value."; "Key/value pairs within tables are not guaranteed to be in any specific order."; "Any reference to an array of tables points to the most recently defined table element of the array."; "Special float values can also be expressed. They are always lowercase." (`inf`, `+inf`, `-inf`, `nan`). https://toml.io/en/v1.0.0
- [F16] TOML v1.1.0 `toml.md` (tag 1.1.0, released 2025-12-24): "Inline tables can have multiple key/value pairs on the same line, or they can be put on different lines. A terminating comma (also called trailing comma) is permitted after the last key/value pair." https://toml.io/en/v1.1.0
- [F17] toml++ v3.4.0 `include/toml++/impl/value.hpp` `node::value<T>()`, `case node_type::integer` → `is_natively_one_of<T, double>` branch: converts the `int64_t` when `std::numeric_limits<T>::digits < 64` and the magnitude is below `1 << digits`; `node.hpp` doc: "matching type (or losslessly convertible to it), or an empty optional".
- [F18] JSON5: https://json5.org/ ("It is not intended to be used for machine-to-machine communication."); The JSON5 Data Interchange Format 1.0.0 §6 numbers ("infinity, negative infinity, and NaN"), §7 comments, §1.1 trailing commas. https://spec.json5.org/
- [F19] microsoft/vscode `main`: `extensions/` has `json`, `json-language-features`, `yaml`, no `json5` or `toml` folder; `extensions/json/package.json` contributes languages `json` (`.json`, …), `jsonc` (`.jsonc`, `.eslintrc`, …), `jsonl`, `snippets`, none with `.json5`. https://github.com/microsoft/vscode/tree/main/extensions ; https://raw.githubusercontent.com/microsoft/vscode/main/extensions/json/package.json ; Visual Studio Marketplace, `mrmlnc.vscode-json5` ("adds syntax highlighting of JSON5 files", 266,109 installs, version 1.0.0, feature list "Syntax highlight" only). https://marketplace.visualstudio.com/items?itemName=mrmlnc.vscode-json5
- [F20] JetBrains Rider help, "JSON": "JetBrains Rider recognizes a number of most popular JSON standards, including JSON5." and "by default treats files with the json5 extension as JSON5 files" https://www.jetbrains.com/help/rider/JSON.html (same wording for IntelliJ IDEA https://www.jetbrains.com/help/idea/json.html ); vim/vim `master` `runtime/syntax/json5.vim` (Language: JSON5, Last Change 2019 Apr 1) https://raw.githubusercontent.com/vim/vim/master/runtime/syntax/json5.vim and `runtime/autoload/dist/ft.vim` extension map `"json5": "json5"`; `runtime/filetype.vim` sets `jsonc` for `.babelrc`, `[jt]sconfig*.json` and others https://raw.githubusercontent.com/vim/vim/master/runtime/autoload/dist/ft.vim ; github-linguist `lib/linguist/languages.yml` `JSON5: type: data, extensions: [".json5"], tm_scope: source.js`. https://raw.githubusercontent.com/github-linguist/linguist/main/lib/linguist/languages.yml
- [F21] `JsonNumberHandling.AllowNamedFloatingPointLiterals` remarks ("can potentially produce JSON that cannot be parsed by other JSON implementations"). https://learn.microsoft.com/en-us/dotnet/api/system.text.json.serialization.jsonnumberhandling
- [F22] git `gitattributes`: built-in `text` merge driver (3-way file-level merge over lines), custom `merge.<driver>.driver` with `%O %A %B`, drivers invoked for the file when a merge is needed. https://git-scm.com/docs/gitattributes
- [F23] Unity manual, "Smart merge" (UnityYAMLMerge): `.git/config` `[merge] tool = unityyamlmerge`, `[mergetool "unityyamlmerge"] trustExitCode = false`, `cmd = '<path to UnityYAMLMerge>' merge -p "$BASE" "$REMOTE" "$LOCAL" "$MERGED"`; merges `.unity`, `.prefab`, `.meta`, others with `--force`; `mergerules.txt` `[arrays]` `set`/`plain` (e.g. `set *.GameObject.m_Component *.fileID`), `[exclusions]`, `[comparisons]` `float <path> [relative] [absolute cutoff]`; `mergespecfile.txt` fallbacks; exit codes 0/1/2/3. https://docs.unity.com/en-us/engine/current/manual/get-started/project-configuration/version-control/smart-merge
- [F24] Godot 4.7 docs, "Version control systems": "Godot aims to be VCS-friendly and generate mostly readable and mergeable files"; `.gitattributes` example puts `*.scn`, `*.res`, `*.material`, `*.anim`, `*.mesh`, `*.lmbake` under `merge=lfs`; no merge tool for `.tscn`/`.tres`. https://docs.godotengine.org/en/stable/tutorials/best_practices/version_control_systems.html
- [F25] jonatanpedersen/git-json-merge README (MIT; `[merge "json"] driver = git-json-merge %A %O %B`, `*.json merge=json`; "uses xdiff to automatically resolve merge conflicts in JSON files") https://github.com/jonatanpedersen/git-json-merge ; suture-driver 5.1.0 README on docs.rs (JSON, YAML, TOML and more; "AGPL-3.0" plus "Suture Commercial License"; install script; array matching and comment handling not stated) https://docs.rs/crate/suture-driver/5.1.0/source/README.md . Web searches on 2026-11-19 for "git merge driver JSON semantic merge key-based" and "git merge driver YAML semantic three-way merge tool" found nothing else general-purpose and maintained; Weave targets code through tree-sitter.
