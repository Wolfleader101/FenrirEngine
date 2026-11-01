# XeGTAO

`builtin:gtao` (`Shaders/Builtin/gtao.frag`), the renderer's ground-truth ambient occlusion (ADR-0049), is a GLSL port
of Intel's [XeGTAO](https://github.com/GameTechDev/XeGTAO) (Filip Strugar, after Jimenez et al. 2016, "Practical
Real-Time Strategies for Accurate Indirect Occlusion"), from `Source/Rendering/Shaders/XeGTAO.hlsli` at commit
`a5b1686c7ea37788eeb3576b5be47f7c03db532c`: its main pass (the horizon search, its falloff, edges and Hilbert-curve R2
noise) and its denoise pass, with its default constants. Left out: the depth MIP chain (depth is read at full
resolution), temporal noise, bent normals and the thickness heuristic, which XeGTAO also leaves off by default.

## Licence

MIT, copyright (C) 2016-2021 Intel Corporation: `LICENSE.txt`, as XeGTAO ships it.
