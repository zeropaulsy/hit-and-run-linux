glslang (KhronosGroup/glslang, tag vulkan-sdk-1.4.341.0, version 16.2.0), BSD-style licences in LICENSE.txt.
Only what GLSL -> SPIR-V needs: glslang/ without HLSL, Windows and Web OS layers and the C interface; SPIRV/ without
its C interface; no SPIRV-Tools optimiser. generated/glslang/build_info.h is build_info.py's output.
shar_glslang.{h,cpp,map} (Linux port) wrap it as libshar_glslang.so, see shar_glslang.h.
