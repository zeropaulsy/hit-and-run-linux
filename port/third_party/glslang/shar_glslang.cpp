// Linux port: GLSL -> SPIR-V for the Vulkan renderer (see shar_glslang.h).

#include "shar_glslang.h"

#include "glslang/Public/ShaderLang.h"
#include "glslang/Public/ResourceLimits.h"
#include "glslang/Include/Types.h"
#include "SPIRV/GlslangToSpv.h"

#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define SHAR_EXPORT extern "C" __attribute__((visibility("default")))

static std::mutex s_mutex;
static bool s_initialised = false;

static void AppendLog(std::string& log, const char* what, const char* text)
{
    if(text && text[0])
    {
        log += what;
        log += text;
    }
}

static unsigned* CopyWords(const std::vector<unsigned>& words)
{
    unsigned* copy = (unsigned*)malloc(words.size() * sizeof(unsigned));
    memcpy(copy, words.data(), words.size() * sizeof(unsigned));
    return copy;
}

SHAR_EXPORT int shar_glsl_compile(const char* name, const char* header, const char* vertexSource,
                                  const char* fragmentHeader, const char* fragmentSource, shar_glsl_program* result)
{
    memset(result, 0, sizeof(*result));
    std::lock_guard<std::mutex> lock(s_mutex);
    if(!s_initialised)
    {
        glslang::InitializeProcess();
        s_initialised = true;
    }

    const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    std::string log;
    bool ok = true;

    // the shaders and program must die before the next compile: glslang's pool allocator is per thread
    {
        glslang::TShader vertex(EShLangVertex), fragment(EShLangFragment);
        glslang::TShader* shaders[2] = { &vertex, &fragment };
        const char* sources[2][2] = { { header ? header : "", vertexSource },
                                      { fragmentHeader ? fragmentHeader : (header ? header : ""), fragmentSource } };
        for(int s = 0; s < 2 && ok; s++)
        {
            glslang::TShader& shader = *shaders[s];
            shader.setStrings(sources[s], 2);
            shader.setEnvInput(glslang::EShSourceGlsl, s == 0 ? EShLangVertex : EShLangFragment,
                               glslang::EShClientVulkan, 100);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
            shader.setEnvInputVulkanRulesRelaxed();
            shader.setGlobalUniformSet(0);
            shader.setGlobalUniformBinding(0);
            shader.setAutoMapBindings(true);
            shader.setAutoMapLocations(true);
            shader.setShiftSamplerBinding(1);   // binding 0 is the uniform block
            ok = shader.parse(GetDefaultResources(), 330, false, messages);
            AppendLog(log, s == 0 ? "vertex:\n" : "fragment:\n", shader.getInfoLog());
        }

        glslang::TProgram program;
        if(ok)
        {
            program.addShader(&vertex);
            program.addShader(&fragment);
            ok = program.link(messages) && program.mapIO();
            AppendLog(log, "link:\n", program.getInfoLog());
        }

        if(ok)
        {
            std::vector<unsigned> spirv;
            glslang::SpvOptions options;
            options.disableOptimizer = true;
            glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), spirv, &options);
            result->vertex = CopyWords(spirv);
            result->vertexWords = spirv.size();
            spirv.clear();
            glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), spirv, &options);
            result->fragment = CopyWords(spirv);
            result->fragmentWords = spirv.size();

            program.buildReflection(EShReflectionAllBlockVariables);
            for(int b = 0; b < program.getNumUniformBlocks(); b++)
            {
                result->blockSize = program.getUniformBlockSize(b);
                result->blockBinding = program.getUniformBlockBinding(b);
            }
            int count = program.getNumUniformVariables();
            result->uniforms = (shar_glsl_uniform*)calloc(count > 0 ? count : 1, sizeof(shar_glsl_uniform));
            for(int i = 0; i < count; i++)
            {
                const glslang::TObjectReflection& u = program.getUniform(i);
                shar_glsl_uniform& out = result->uniforms[result->uniformCount++];
                std::string n = u.name;
                size_t bracket = n.find('[');
                if(bracket != std::string::npos)
                    n.resize(bracket);
                size_t dot = n.rfind('.');      // members of the default block may come qualified
                if(dot != std::string::npos)
                    n = n.substr(dot + 1);
                strncpy(out.name, n.c_str(), sizeof(out.name) - 1);
                const glslang::TType* type = u.getType();
                bool sampler = type && type->getBasicType() == glslang::EbtSampler;
                out.offset = sampler ? -1 : u.offset;
                out.glType = u.glDefineType;
                out.arraySize = u.size > 0 ? u.size : 1;
                out.arrayStride = out.arraySize > 1 && u.arrayStride < 16 ? 16 : u.arrayStride;     // std140 (scalar arrays reflect 4)
                out.binding = sampler ? u.getBinding() : -1;
                out.stages = ((u.stages & EShLangVertexMask) ? 1 : 0) | ((u.stages & EShLangFragmentMask) ? 2 : 0);
            }
        }
    }

    if(!log.empty())
    {
        std::string full = std::string(name ? name : "shader") + ":\n" + log;
        result->log = strdup(full.c_str());
    }
    return ok ? 1 : 0;
}

SHAR_EXPORT void shar_glsl_free(shar_glsl_program* result)
{
    free(result->vertex);
    free(result->fragment);
    free(result->uniforms);
    free(result->log);
    memset(result, 0, sizeof(*result));
}
