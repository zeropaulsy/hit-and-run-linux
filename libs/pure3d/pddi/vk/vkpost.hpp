//=============================================================================
// Linux port: user post-processing shaders on the Vulkan renderer (vkpost.cpp; as gl/glpost.hpp)
//=============================================================================

#ifndef _VKPOST_HPP_
#define _VKPOST_HPP_

#include <pddi/gl/glpost.hpp>       // pglPostStage

class pvkPipeline;

// the stage's passes over the frame (the scene stage before the GUI layer, the final one before presenting)
void pvkPostRun(int stage, pvkPipeline* pipeline);

#endif
