fips_begin_app(RenderToCubeMap windowed)
    fips_vs_warning_level(3)
    fips_files(RenderToCubeMap.cc)
    oryol_shader(shaders.glsl)
    fips_deps(Gfx Assets Input)
    oryol_add_web_sample(RenderToCubeMap "Render to cubemap faces" "emscripten" RenderToCubeMap.jpg "RenderToCubeMap/RenderToCubeMap.cc")
fips_end_app()
