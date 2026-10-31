# Linearly transformed cosines

The two 64 x 64 lookup tables that `builtin:lit` and every user shader on `Fenrir/Lighting.glsl` shade rect lights with (ADR-0048): the GGX fit of [Real-Time Polygonal-Light Shading with Linearly Transformed Cosines](https://eheitzresearch.wordpress.com/415-2/) (Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt, ACM Transactions on Graphics 35(4), SIGGRAPH 2016), from its authors' code, [selfshadow/ltc_code](https://github.com/selfshadow/ltc_code) at commit `5c0770b74114b5dd38e9dae1b93f8486af7eac1b`:

| File | SHA-256 | Texels (RGBA16F) |
| --- | --- | --- |
| `fit/results/ltc_1.dds` | `6aa389c6c0403a408cf6bb23dcdc543ad3e97f0fdc49e60f034b6a85682ee18d` | The inverse transform's four varying entries, normalised by its middle one |
| `fit/results/ltc_2.dds` | `2ff4f03b10d73e25985216d73a58aa57375f0c775dee0e1bde075d44e2b07200` | The fit's GGX norm and Fresnel term, 0, and the horizon-clipped sphere's form factor (Hill and Heitz 2016) |

Each is laid out by perceptual roughness across and sqrt(1 - N.V) down, and is copied texel for texel into the generated header `src/Core/LtcTables.hpp`, which is committed; the build never runs Python. The renderer uploads them as two RGBA16F textures at set 0 bindings 5 and 6, and `Core/RectLights` reads the same texels for its CPU mirror of the shading.

## Regenerating

```sh
python packages/FenrirRenderer/Resources/Ltc/generate_ltc_tables.py
```

It downloads both files from the pinned commit, refuses either whose SHA-256 differs, and rewrites the header. Where Python cannot download them (a certificate store it cannot read), download them another way and pass their directory: `generate_ltc_tables.py <directory>`.

## Licence

The reference code and its results are under the BSD-style licence in `LICENSE.txt`, copied from the repository: redistribution is allowed with the copyright notice kept and the paper cited, which this file and the header do. Checked on 2026-10-31 for lighting ticket 09.
