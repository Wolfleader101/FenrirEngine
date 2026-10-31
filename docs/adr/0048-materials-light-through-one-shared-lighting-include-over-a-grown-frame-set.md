---
status: proposed
---

# Materials light through one shared lighting include over a grown frame set

ADR-0024 lets a user shader be a whole fragment program, and puts the lighting inside `builtin:lit`. With many lights, shadows, image-based lighting and ambient occlusion, a user shader that wrote its own lighting would be stuck with one light forever, so the lighting moves into an engine include every material can call. This amends ADR-0024.

## The include

- **`Fenrir/Lighting.glsl`** declares a `FenrirSurface` (base colour, alpha, world normal, metallic, roughness, occlusion, emissive) and one function that returns the lit linear HDR colour of it: the directional light with its shadow, the point, spot and rect lights of the pixel's cluster with theirs, the sky's image-based lighting, and ambient occlusion. `builtin:lit` is its first user; a user shader fills a surface its own way and calls it.
- **It is not a surface-function model.** The user shader still owns `main`, its inputs and its outputs, and may ignore the include. Forward rendering is what makes this cheap: there is no G-buffer to fill.
- **Set 0, the frame, grows** to hold everything the include reads: the light buffer and cluster lists (ADR-0049), the shadow atlases and cascade data (ADR-0050), the sky's coefficients, prefiltered cubemap and BRDF lookup table (ADR-0047), and the view's ambient-occlusion texture. The renderer keeps declaring the same layout in C++ and checking it against reflection in debug builds.

## The lights

- **`DirectionalLight` stays one per scene**, the sun; extras are still warned about once.
- **`PointLight { colour, intensity, range }`, `SpotLight { colour, intensity, range, innerAngle, outerAngle }` and `RectLight { colour, intensity, width, height, twoSided, range }`**, any number of each.
- **Falloff is inverse-square, windowed to zero at `range`** (glTF's `KHR_lights_punctual` and Unreal), not LearnOpenGL's constant, linear and quadratic terms. `range` also bounds the light for clustering and its shadow's far plane.
- **Rect lights are shaded with linearly transformed cosines** (Heitz et al. 2016) from two 64×64 lookup tables committed as a generated header. They cast no shadows. Disc, tube and sphere lights are left for later.
- **Intensities stay plain multipliers**, as ADR-0024 made them, and exposure is in EV stops on the camera (ADR-0049). Physical units (lux, lumens, candela with an EV100 camera) were considered and deferred: they would make captured skies and measured lights just work, but would retune every scene. Revisit when a game needs measured lighting.

## The BRDF

- **Height-correlated Smith visibility** (Heitz 2014, as Filament) replaces the separable Schlick-GGX term.
- **Multi-scatter energy compensation** (Fdez-Agüera 2019) stops rough metals losing energy, read from the same BRDF lookup table as the split sum.
- **Specular anti-aliasing** (Tokuyoshi and Kaplanyan) widens roughness by the normal's screen-space variance, so normal-mapped highlights do not shimmer.
- **`lit` gains `occlusionTexture`** (glTF's red channel, indirect light only) and **`emissiveStrength`** (`KHR_materials_emissive_strength`). Clearcoat, sheen, transmission and anisotropy are later material features.
- **Normal maps prefer a tangent stream.** A mesh imported with tangents (glTF's, which are MikkTSpace) uses them; one without keeps the derivative frame.

These change every lit scene slightly, which is accepted.
