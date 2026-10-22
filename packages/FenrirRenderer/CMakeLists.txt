# FenrirRenderer (ADR-0016, ADR-0018). Built with FENRIR_ENABLE_RENDERER. So far it holds the build-time
# FenrirShaderCompiler (Tools/ShaderCompiler) and the engine's shader includes (Shaders/include, the directory
# fenrir_add_shaders resolves #include against); the renderer's own targets join it in later tickets.
add_subdirectory(Tools/ShaderCompiler)
