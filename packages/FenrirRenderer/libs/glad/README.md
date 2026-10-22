# glad

The OpenGL loader of FenrirRenderer's OpenGL backend: [glad 2](https://github.com/Dav1dde/glad) 2.0.8, for OpenGL 4.6 core with no extensions, in its multiple-context (`--mx`) form, so every GL function is called through a `GladGLContext` the backend owns. It is generated once by hand and checked in; the build never runs Python. CMake builds it as the static target `FenrirGlad`, which only the OpenGL backend links, `PRIVATE` (ADR-0016, spec "Build").

The files are glad's output unchanged, apart from line endings (LF, as `.gitattributes` stores C sources).

## Regenerating

In a throwaway Python virtual environment, outside the build:

```sh
python -m venv glad-venv
glad-venv/Scripts/python -m pip install glad2==2.0.8      # glad-venv/bin/python on Linux
glad-venv/Scripts/glad --quiet --reproducible --api gl:core=4.6 --extensions "" --out-path glad-out c --mx
```

Then replace `include/` and `src/` here with `glad-out/include/` and `glad-out/src/`. `--reproducible` uses the GL specification bundled with that glad release instead of fetching the newest one, so the same command gives the same files. `--extensions ""` leaves every extension out: what the backend uses (`KHR_debug`, clip control, SPIR-V shaders, DSA) is core in 4.6.

## Licence

The generated files are `(WTFPL OR CC0-1.0) AND Apache-2.0`, as their headers say; `khrplatform.h` is the Khronos Group's, under its own notice.
