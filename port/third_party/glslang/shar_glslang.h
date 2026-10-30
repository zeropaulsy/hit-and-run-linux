/* Linux port: GLSL -> SPIR-V for the Vulkan renderer (libs/pure3d/pddi/vk), built on glslang as its own shared
   library (libshar_glslang.so). glslang allocates with C++ new throughout; inside the game binary that would come
   from the game's fixed-size heaps (code/memory/srrmemory.cpp), so it lives in a library that cannot see the game's
   operator new and is called through this C interface.

   Shaders are the GL renderer's GLSL (#version 330, loose uniforms), compiled with GL_EXT_vulkan_glsl_relaxed
   rules: the uniforms that are not samplers go into one uniform block (set 0, binding 0), samplers get bindings
   from 1 up in the order they are declared, and the vertex outputs are matched to the fragment inputs by name.
   The reflection tells the caller where each uniform sits in the block (std140), so it can fill the block as GL
   took glUniform* calls. */

#ifndef SHAR_GLSLANG_H
#define SHAR_GLSLANG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char name[64];      /* arrays without "[0]" */
    int offset;         /* bytes into the uniform block; -1 for samplers */
    int glType;         /* GL_FLOAT_VEC4, GL_SAMPLER_2D, ... */
    int arraySize;      /* 1 unless an array */
    int arrayStride;    /* std140: 16 for every array element */
    int binding;        /* samplers: the descriptor binding (set 0) */
    int stages;         /* 1 vertex, 2 fragment */
} shar_glsl_uniform;

typedef struct
{
    unsigned* vertex;           /* SPIR-V words */
    size_t vertexWords;
    unsigned* fragment;
    size_t fragmentWords;
    int blockSize;              /* bytes of the uniform block (0: none) */
    int blockBinding;           /* 0 */
    int uniformCount;
    shar_glsl_uniform* uniforms;
    char* log;                  /* errors and warnings; NULL when there were none */
} shar_glsl_program;

/* Compiles and links. header (may be NULL) goes before both sources, fragmentHeader (may be NULL) before the
   fragment source instead of header. Returns 1 on success; on failure 0, with the reason in result->log. Thread
   safe. Free the result with shar_glsl_free either way. */
int shar_glsl_compile(const char* name, const char* header, const char* vertexSource, const char* fragmentHeader,
                      const char* fragmentSource, shar_glsl_program* result);
void shar_glsl_free(shar_glsl_program* result);

#ifdef __cplusplus
}
#endif

#endif
