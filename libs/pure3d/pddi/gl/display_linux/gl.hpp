//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>
// this is neccesary because of windows api issues

// Linux port: the renderer runs on an OpenGL 3.3 core context (glpipeline.hpp), so it includes the core-profile
// header: a call to a function core OpenGL no longer has (glBegin, glLight, glMatrixMode, ...) does not compile.
// glvnd's libGL exports every core entry point, so no loader is needed.

#ifndef _PGL_GL_HPP_
#define _PGL_GL_HPP_

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/glcorearb.h>
#endif

#endif
