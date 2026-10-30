//=============================================================================
// Linux port: OpenGL 3.3 core renderer (see glpipeline.hpp).
//=============================================================================

#include <pddi/gl/glpipeline.hpp>
#include <pddi/gl/glshaders.hpp>

#include <SDL2/SDL.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------------------------------------------------------------------
// pglProgram
//-----------------------------------------------------------------------------

pglProgram::pglProgram() : program(0)
{
}

pglProgram::~pglProgram()
{
    // the context is gone by the time the display is destroyed; the driver frees the program with it
}

static GLuint CompileShader(const char* name, GLenum type, const char* header, const char* source)
{
    GLuint shader = glCreateShader(type);
    const char* sources[2] = { header, source };
    if(header)
        glShaderSource(shader, 2, sources, NULL);
    else
        glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if(!ok)
    {
        char log[2048] = "";
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "PDDI: %s %s shader does not compile:\n%s\n", name,
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool pglProgram::Build(const char* name, const char* vertexSource, const char* fragmentSource, const char* header,
                       const char* fragmentHeader)
{
    GLuint vs = CompileShader(name, GL_VERTEX_SHADER, header, vertexSource);
    GLuint fs = CompileShader(name, GL_FRAGMENT_SHADER, fragmentHeader ? fragmentHeader : header, fragmentSource);
    if(!vs || !fs)
    {
        if(vs) glDeleteShader(vs);
        if(fs) glDeleteShader(fs);
        return false;
    }

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, PGL_ATTRIB_POSITION, "aPosition");
    glBindAttribLocation(program, PGL_ATTRIB_NORMAL, "aNormal");
    glBindAttribLocation(program, PGL_ATTRIB_COLOUR, "aColour");
    glBindAttribLocation(program, PGL_ATTRIB_UV, "aUV");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if(!ok)
    {
        char log[2048] = "";
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "PDDI: %s program does not link:\n%s\n", name, log);
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    return true;
}

void pglProgram::Release()
{
    if(program)
        glDeleteProgram(program);
    program = 0;
}

GLint pglProgram::Uniform(const char* name) const
{
    return glGetUniformLocation(program, name);
}


static void Identity(float* m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void SetColour(float* d, const float* s, int n)
{
    for(int i = 0; i < n; i++)
        d[i] = s[i];
}

pglPipeline::pglPipeline()
{
    ready = false;
    customCount = 0;
    frameCount = 0;
    frameTime = 0.0f;
    frameWidth = frameHeight = 1.0f;
    multisampled = false;
    mipAlpha = 0.0f;
    alphaToCoverageState = -1;
    bound = NULL;
    vao = 0;
    screenTexture = screenFbo = 0;
    screenWidth = screenHeight = 0;
    screenFrame = 0;
    streamVerts = NULL;
    streamCount = streamCapacity = 0;
    streamPrim = GL_TRIANGLES;
    streamBuffer = 0;
    streamBufferSize = streamOffset = 0;
    memset(version, 0, sizeof(version));

    // the initial state
    Identity(projection);
    Identity(modelView);
    memset(lights, 0, sizeof(lights));
    for(int i = 0; i < pglMaxLights; i++)
    {
        lights[i].atten[0] = 1.0f;
        lights[i].range = 1000000.0f;
    }
    ambient[0] = ambient[1] = ambient[2] = 0.2f;
    ambient[3] = 1.0f;
    fogEnabled = false;
    fogColour[0] = fogColour[1] = fogColour[2] = 0.0f;
    fogStart = 0.0f;
    fogEnd = 1.0f;

    memset(&material, 0, sizeof(material));
    material.alphaCompare = -1;
    material.custom = -1;

    memset(&stats, 0, sizeof(stats));
    memset(&current, 0, sizeof(current));
    current.normal[2] = 1.0f;
    current.colour[0] = current.colour[1] = current.colour[2] = current.colour[3] = 255;
}

pglPipeline::~pglPipeline()
{
    free(streamVerts);
}

bool pglPipeline::Init()
{
    if(ready)
        return true;

    for(int i = 0; i < PGL_PROGRAM_COUNT; i++)
    {
        if(!BuildProgram(programs[i], pglProgramSources[i].name, pglProgramSources[i].header, pglFragmentMain, false, NULL, NULL))
            return false;
    }
    bound = NULL;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(PGL_ATTRIB_POSITION);     // every draw has positions

    glGenBuffers(1, &streamBuffer);
    streamBufferSize = 4 * 1024 * 1024;
    glBindBuffer(GL_ARRAY_BUFFER, streamBuffer);
    glBufferData(GL_ARRAY_BUFFER, streamBufferSize, NULL, GL_STREAM_DRAW);
    streamOffset = 0;

    ready = true;
    return true;
}

// joins NULL-terminated parts into a malloc'd string
static char* Join(const char** parts)
{
    size_t size = 1;
    for(int i = 0; parts[i]; i++)
        size += strlen(parts[i]);
    char* s = (char*)malloc(size);
    if(!s)
        return NULL;
    s[0] = 0;
    for(int i = 0; parts[i]; i++)
        strcat(s, parts[i]);
    return s;
}

bool pglPipeline::BuildProgram(Program& p, const char* name, const char* header, const char* fragmentBody, bool custom,
                               const char* fragmentHeader, const char* vertexBody)
{
    // vertex: the declarations, a custom shader's vertex part (with its own line numbers), main; fragment: the
    // prelude, the body (a custom one with its own line numbers, then main)
    const char* vertexParts[] = { pglVertexDeclarations, vertexBody ? "#line 1\n" : "", vertexBody ? vertexBody : "",
                                  vertexBody ? "\n" : "", pglVertexMain, NULL };
    const char* fragmentParts[] = { pglFragmentPrelude, custom ? "#line 1\n" : "", fragmentBody,
                                    custom ? pglCustomFragmentMain : "", NULL };
    char* vertex = Join(vertexParts);
    char* fragment = Join(fragmentParts);
    bool ok = vertex && fragment && p.program.Build(name, vertex, fragment, header, fragmentHeader);
    free(vertex);
    free(fragment);
    if(!ok)
        return false;

    glUseProgram(p.program.Handle());
    for(int u = 0; u < U_COUNT; u++)
        p.uniform[u] = p.program.Uniform(pglUniformNames[u]);
    glUniform1i(p.uniform[U_TEXTURE], 0);
    glUniform1i(p.uniform[U_REFLECTION], 1);
    glUniform1i(p.uniform[U_SCREEN], 2);
    for(int g = 0; g < GROUP_COUNT; g++)
        p.uploaded[g] = version[g] - 1;     // everything to upload
    p.timeFrame = frameCount - 1;
    p.cull = 0;
    bound = NULL;
    return true;
}

// "#pragma shar_cull none" in a custom shader: it draws every face, whatever the game's culling
static int CullDirective(const char* source)
{
    const char* p = strstr(source, "#pragma shar_cull");
    if(!p)
        return PGL_CULL_GAME;
    p += strlen("#pragma shar_cull");
    while(*p == ' ' || *p == '\t')
        p++;
    return strncmp(p, "none", 4) == 0 ? PGL_CULL_NONE : PGL_CULL_GAME;
}

int pglPipeline::BuildCustomProgram(const char* name, const char* source)
{
    if(!ready || customCount == pglMaxCustomPrograms)
        return -1;
    Program& p = programs[PGL_PROGRAM_COUNT + customCount];

    // the file goes into the fragment stage with FRAGMENT_STAGE defined; if it defines MaterialVertex, also into the
    // vertex stage with VERTEX_STAGE defined (the file keeps each part inside #ifdef ..._STAGE)
    bool vertexPart = strstr(source, "MaterialVertex") != NULL;
    char vertexHeader[160], fragmentHeader[160];
    snprintf(vertexHeader, sizeof(vertexHeader),
             "#version 330 core\n#define REFLECTION 1\n#define REFRACTION 0\n#define CUSTOM 1\n#define MATERIAL_VERTEX %d\n#define VERTEX_STAGE 1\n",
             vertexPart ? 1 : 0);
    snprintf(fragmentHeader, sizeof(fragmentHeader),
             "#version 330 core\n#define REFLECTION 1\n#define REFRACTION 0\n#define CUSTOM 1\n#define FRAGMENT_STAGE 1\n");
    if(!BuildProgram(p, name, vertexHeader, source, true, fragmentHeader, vertexPart ? source : NULL))
        return -1;
    p.cull = CullDirective(source);
    return customCount++;
}

int pglPipeline::CustomCull(int slot) const
{
    return slot >= 0 && slot < customCount ? programs[PGL_PROGRAM_COUNT + slot].cull : PGL_CULL_GAME;
}

void pglPipeline::ReleaseCustomPrograms()
{
    for(int i = 0; i < customCount; i++)
        programs[PGL_PROGRAM_COUNT + i].program.Release();
    customCount = 0;
    bound = NULL;
    material.custom = -1;
    Changed(GROUP_MATERIAL);
}

void pglPipeline::NewFrame(int width, int height)
{
    frameCount++;
    frameTime = SDL_GetTicks() / 1000.0f;
    frameWidth = (float)width;
    frameHeight = (float)height;
}

// setters skip values that did not change: the game sets the same material and matrices over and over
void pglPipeline::SetProjection(const float* m)
{
    if(memcmp(projection, m, sizeof(projection)) == 0)
        return;
    memcpy(projection, m, sizeof(projection));
    Changed(GROUP_MATRICES);
}

void pglPipeline::SetModelView(const float* m)
{
    if(memcmp(modelView, m, sizeof(modelView)) == 0)
        return;
    memcpy(modelView, m, sizeof(modelView));
    Changed(GROUP_MATRICES);
}

void pglPipeline::LoadIdentity()
{
    Identity(projection);
    Identity(modelView);
    Changed(GROUP_MATRICES);
}

void pglPipeline::SetLight(int i, bool enabled, const float* p, const float* colour, float attenA, float attenB, float attenC,
                           float range)
{
    if(i < 0 || i >= pglMaxLights)
        return;
    Light& l = lights[i];
    l.enabled = enabled;
    for(int r = 0; r < 4; r++)
        l.position[r] = modelView[r] * p[0] + modelView[4 + r] * p[1] + modelView[8 + r] * p[2] + modelView[12 + r] * p[3];
    SetColour(l.colour, colour, 3);
    l.atten[0] = attenA;
    l.atten[1] = attenB;
    l.atten[2] = attenC;
    l.range = range;
    Changed(GROUP_LIGHTS);
}

void pglPipeline::SetAmbient(const float* colour)
{
    SetColour(ambient, colour, 4);
    Changed(GROUP_LIGHTS);
}

void pglPipeline::EnableFog(bool enable)
{
    fogEnabled = enable;
    Changed(GROUP_FOG);
}

void pglPipeline::SetFog(const float* colour, float start, float end)
{
    SetColour(fogColour, colour, 3);
    fogStart = start;
    fogEnd = end;
    Changed(GROUP_FOG);
}

void pglPipeline::SetMaterial(const Material& m)      // m fully initialised (memset), padding included
{
    if(memcmp(&material, &m, sizeof(material)) == 0)
        return;
    material = m;
    Changed(GROUP_MATERIAL);
}

void pglPipeline::UploadUniforms(Program& p)
{
    const GLint* u = p.uniform;

    if(p.uploaded[GROUP_MATRICES] != version[GROUP_MATRICES])
    {
        stats.matrixUploads++;
        glUniformMatrix4fv(u[U_PROJECTION], 1, GL_FALSE, projection);
        glUniformMatrix4fv(u[U_MODELVIEW], 1, GL_FALSE, modelView);

        // normals go through the inverse transpose of the model-view's upper 3x3: its columns are the cross
        // products of the matrix's columns, over the determinant
        const float* a0 = &modelView[0];
        const float* a1 = &modelView[4];
        const float* a2 = &modelView[8];
        float n[9];
        n[0] = a1[1] * a2[2] - a1[2] * a2[1];
        n[1] = a1[2] * a2[0] - a1[0] * a2[2];
        n[2] = a1[0] * a2[1] - a1[1] * a2[0];
        n[3] = a2[1] * a0[2] - a2[2] * a0[1];
        n[4] = a2[2] * a0[0] - a2[0] * a0[2];
        n[5] = a2[0] * a0[1] - a2[1] * a0[0];
        n[6] = a0[1] * a1[2] - a0[2] * a1[1];
        n[7] = a0[2] * a1[0] - a0[0] * a1[2];
        n[8] = a0[0] * a1[1] - a0[1] * a1[0];
        float det = a0[0] * n[0] + a0[1] * n[1] + a0[2] * n[2];
        if(det != 0.0f)
        {
            for(int i = 0; i < 9; i++)
                n[i] /= det;
        }
        glUniformMatrix3fv(u[U_NORMALMATRIX], 1, GL_FALSE, n);
        p.uploaded[GROUP_MATRICES] = version[GROUP_MATRICES];
    }

    if(p.uploaded[GROUP_LIGHTS] != version[GROUP_LIGHTS])
    {
        stats.lightUploads++;
        float position[pglMaxLights][4], colour[pglMaxLights][3], atten[pglMaxLights][3], range[pglMaxLights];
        int count = 0;
        for(int i = 0; i < pglMaxLights; i++)
        {
            if(!lights[i].enabled)
                continue;
            memcpy(position[count], lights[i].position, sizeof(position[0]));
            memcpy(colour[count], lights[i].colour, sizeof(colour[0]));
            memcpy(atten[count], lights[i].atten, sizeof(atten[0]));
            range[count] = lights[i].range;
            count++;
        }
        glUniform1i(u[U_LIGHTCOUNT], count);
        if(count)
        {
            glUniform4fv(u[U_LIGHTPOSITION], count, &position[0][0]);
            glUniform3fv(u[U_LIGHTCOLOUR], count, &colour[0][0]);
            glUniform3fv(u[U_LIGHTATTEN], count, &atten[0][0]);
            glUniform1fv(u[U_LIGHTRANGE], count, range);
        }
        glUniform4fv(u[U_GLOBALAMBIENT], 1, ambient);
        p.uploaded[GROUP_LIGHTS] = version[GROUP_LIGHTS];
    }

    if(p.uploaded[GROUP_FOG] != version[GROUP_FOG])
    {
        glUniform1i(u[U_FOG], fogEnabled);
        glUniform3fv(u[U_FOGCOLOUR], 1, fogColour);
        glUniform1f(u[U_FOGEND], fogEnd);
        glUniform1f(u[U_FOGSCALE], fogEnd != fogStart ? 1.0f / (fogEnd - fogStart) : 1.0f);
        p.uploaded[GROUP_FOG] = version[GROUP_FOG];
    }

    if(p.uploaded[GROUP_MATERIAL] != version[GROUP_MATERIAL])
    {
        stats.materialUploads++;
        glUniform1i(u[U_TEXTURED], material.textured);
        glUniform1i(u[U_LIT], material.lit);
        glUniform4fv(u[U_MATAMBIENT], 1, material.ambient);
        glUniform4fv(u[U_MATDIFFUSE], 1, material.diffuse);
        glUniform4fv(u[U_MATSPECULAR], 1, material.specular);
        glUniform4fv(u[U_MATEMISSIVE], 1, material.emissive);
        glUniform1f(u[U_SHININESS], material.shininess);
        glUniform1i(u[U_ALPHACOMPARE], material.alphaCompare < 0 ? 1 : material.alphaCompare);    // 1: always
        glUniform1f(u[U_ALPHAREF], material.alphaRef);
        glUniform1i(u[U_ALPHATOCOVERAGE], AlphaToCoverage());
        glUniform1f(u[U_MIPALPHA], material.blended ? 0.0f : mipAlpha);
        if(u[U_ENVBLEND] >= 0)
        {
            glUniform1i(u[U_REFLECTIONTEXTURED], material.reflectionTextured);
            glUniform4fv(u[U_ENVBLEND], 1, material.envBlend);
        }
        if(u[U_REFRACTINDEX] >= 0)
        {
            glUniform4fv(u[U_REFRACTCOLOUR], 1, material.refractColour);
            glUniform1f(u[U_REFRACTINDEX], material.refractIndex);
            glUniform1f(u[U_REFRACTBLEND], material.refractBlend);
        }
        p.uploaded[GROUP_MATERIAL] = version[GROUP_MATERIAL];
    }
}

static bool s_recaptureScreen = false;     // pglRecaptureScreen

void pglRecaptureScreen()
{
    s_recaptureScreen = true;
}

void pglPipeline::CaptureScreen()
{
    int width = (int)frameWidth, height = (int)frameHeight;
    if(screenFrame != frameCount || screenTexture == 0 || s_recaptureScreen)
    {
        screenFrame = frameCount;
        s_recaptureScreen = false;
        glActiveTexture(GL_TEXTURE2);
        if(screenTexture == 0)
        {
            glGenTextures(1, &screenTexture);
            glGenFramebuffers(1, &screenFbo);
        }
        GLint drawFbo = 0, readFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        if(width != screenWidth || height != screenHeight)
        {
            screenWidth = width;
            screenHeight = height;
            glBindTexture(GL_TEXTURE_2D, screenTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, screenFbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture, 0);
        }
        // the frame so far (resolved, when multisampled); blits ignore write masks but not the scissor
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        if(scissor)
            glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, screenFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        if(scissor)
            glEnable(GL_SCISSOR_TEST);
    }
    else
    {
        glActiveTexture(GL_TEXTURE2);
    }
    glBindTexture(GL_TEXTURE_2D, screenTexture);
    glActiveTexture(GL_TEXTURE0);
}

void pglPipeline::Prepare()
{
    int index = material.program >= 0 && material.program < PGL_PROGRAM_COUNT ? material.program : PGL_PROGRAM_SIMPLE;
    if(material.custom >= 0 && material.custom < customCount)
        index = PGL_PROGRAM_COUNT + material.custom;
    Program& p = programs[index];
    if(bound != &p)
    {
        if(!bound)
            glBindVertexArray(vao);
        glUseProgram(p.program.Handle());
        bound = &p;
    }
    UploadUniforms(p);
    if(p.timeFrame != frameCount && (p.uniform[U_TIME] >= 0 || p.uniform[U_RESOLUTION] >= 0))
    {
        glUniform1f(p.uniform[U_TIME], frameTime);
        glUniform2f(p.uniform[U_RESOLUTION], frameWidth, frameHeight);
        p.timeFrame = frameCount;
    }

    int alphaToCoverage = AlphaToCoverage();
    if(alphaToCoverage != alphaToCoverageState)
    {
        if(alphaToCoverage)
            glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        else
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        alphaToCoverageState = alphaToCoverage;
    }
    stats.draws++;
}

// for alpha tests that keep the high alphas (the data's: GREATER, a few GREATEREQUAL) on opaque materials
bool pglPipeline::AlphaToCoverage() const
{
    return multisampled && !material.blended &&
           (material.alphaCompare == 4 || material.alphaCompare == 5);     // PDDI_COMPARE_GREATER, _GREATEREQUAL
}

void pglPipeline::SetTextureFiltering(bool mipmaps, float anisotropy)
{
    float value = mipmaps ? (anisotropy < 1.0f ? 1.0f : anisotropy) : 0.0f;
    if(value == mipAlpha)
        return;
    mipAlpha = value;
    Changed(GROUP_MATERIAL);
}

void pglPipeline::SetMultisampled(bool m)
{
    static int s_allowed = -1;      // SHAR_ALPHA_TO_COVERAGE=0: plain alpha test even with MSAA
    if(s_allowed < 0)
    {
        const char* e = getenv("SHAR_ALPHA_TO_COVERAGE");
        s_allowed = !(e && e[0] == '0');
    }
    m = m && s_allowed;
    if(m == multisampled)
        return;
    multisampled = m;
    Changed(GROUP_MATERIAL);        // uAlphaToCoverage
}

static int s_statsEnabled = -1;
static GLuint s_timeQuery[2];
static int s_timeQueryIndex = 0;
static bool s_timeQueryUsed[2];
static Uint64 s_frameStart;

void pglPipeline::FrameStats()
{
    static Uint32 last = 0;
    if(s_statsEnabled < 0)
    {
        const char* e = getenv("SHAR_GL_STATS");
        s_statsEnabled = e && e[0] == '1';
        last = SDL_GetTicks();
        if(s_statsEnabled)
            glGenQueries(2, s_timeQuery);
    }
    if(!s_statsEnabled)
        return;

    // the other query is a frame old: its result is (nearly always) in
    int other = s_timeQueryIndex ^ 1;
    if(s_timeQueryUsed[other])
    {
        GLuint64 ns = 0;
        glGetQueryObjectui64v(s_timeQuery[other], GL_QUERY_RESULT, &ns);
        stats.gpuMs += ns / 1e6;
        if(ns / 1e6 > stats.gpuMaxMs)
            stats.gpuMaxMs = ns / 1e6;
        stats.gpuFrames++;
        s_timeQueryUsed[other] = false;
    }
    glBeginQuery(GL_TIME_ELAPSED, s_timeQuery[s_timeQueryIndex]);
    s_frameStart = SDL_GetPerformanceCounter();

    stats.frames++;
    if(SDL_GetTicks() - last < 5000)
        return;
    last = SDL_GetTicks();
    float f = (float)stats.frames;
    fprintf(stderr, "[gl] per frame: %.0f draws (%.0f streamed), uniforms: %.0f matrix %.0f light %.0f material, "
            "%.1f buffer uploads (%.0f KB); render time %.3f ms CPU, %.3f ms GPU (slowest %.1f)\n", stats.draws / f, stats.streamDraws / f,
            stats.matrixUploads / f, stats.lightUploads / f, stats.materialUploads / f, stats.bufferUploads / f,
            stats.bufferBytes / f / 1024.0f, stats.cpuMs / f, stats.gpuFrames ? stats.gpuMs / stats.gpuFrames : 0.0, stats.gpuMaxMs);
    memset(&stats, 0, sizeof(stats));
}

void pglPipeline::FrameEnd()
{
    if(s_statsEnabled <= 0)
        return;
    glEndQuery(GL_TIME_ELAPSED);
    s_timeQueryUsed[s_timeQueryIndex] = true;
    s_timeQueryIndex ^= 1;
    stats.cpuMs += (SDL_GetPerformanceCounter() - s_frameStart) * 1000.0 / SDL_GetPerformanceFrequency();
}

void pglPipeline::UseCurrent(GLuint attrib)
{
    glDisableVertexAttribArray(attrib);
    switch(attrib)
    {
        case PGL_ATTRIB_NORMAL:
            glVertexAttrib3f(attrib, current.normal[0], current.normal[1], current.normal[2]);
            break;
        case PGL_ATTRIB_COLOUR:
            glVertexAttrib4f(attrib, current.colour[0] / 255.0f, current.colour[1] / 255.0f,
                             current.colour[2] / 255.0f, current.colour[3] / 255.0f);
            break;
        case PGL_ATTRIB_UV:
            glVertexAttrib2f(attrib, current.uv[0], current.uv[1]);
            break;
    }
}

void pglPipeline::BeginStream(GLenum primType)
{
    streamPrim = primType;
    streamCount = 0;
}

void pglPipeline::Emit(float x, float y, float z)
{
    if(streamCount == streamCapacity)
    {
        int capacity = streamCapacity ? streamCapacity * 2 : 1024;
        pglVertex* v = (pglVertex*)realloc(streamVerts, capacity * sizeof(pglVertex));
        if(!v)
            return;
        streamVerts = v;
        streamCapacity = capacity;
    }
    pglVertex& v = streamVerts[streamCount++];
    v = current;
    v.position[0] = x;
    v.position[1] = y;
    v.position[2] = z;
}

void pglPipeline::EndStream()
{
    if(!ready || streamCount == 0)
        return;

    Prepare();

    // append to the ring buffer without waiting for draws still reading it; when full, orphan it (the driver hands
    // out fresh storage) and start again at the beginning
    unsigned size = streamCount * sizeof(pglVertex);
    glBindBuffer(GL_ARRAY_BUFFER, streamBuffer);
    if(size > streamBufferSize)
    {
        while(streamBufferSize < size)
            streamBufferSize *= 2;
        glBufferData(GL_ARRAY_BUFFER, streamBufferSize, NULL, GL_STREAM_DRAW);
        streamOffset = 0;
    }
    else if(streamOffset + size > streamBufferSize)
    {
        glBufferData(GL_ARRAY_BUFFER, streamBufferSize, NULL, GL_STREAM_DRAW);
        streamOffset = 0;
    }
    glBufferSubData(GL_ARRAY_BUFFER, streamOffset, size, streamVerts);

    GLsizei stride = sizeof(pglVertex);
    const char* base = (const char*)(size_t)streamOffset;
    glVertexAttribPointer(PGL_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, stride, base + offsetof(pglVertex, position));
    glVertexAttribPointer(PGL_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, base + offsetof(pglVertex, normal));
    glVertexAttribPointer(PGL_ATTRIB_COLOUR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, base + offsetof(pglVertex, colour));
    glVertexAttribPointer(PGL_ATTRIB_UV, 2, GL_FLOAT, GL_FALSE, stride, base + offsetof(pglVertex, uv));
    glEnableVertexAttribArray(PGL_ATTRIB_NORMAL);
    glEnableVertexAttribArray(PGL_ATTRIB_COLOUR);
    glEnableVertexAttribArray(PGL_ATTRIB_UV);

    glDrawArrays(streamPrim, 0, streamCount);
    stats.streamDraws++;
    streamOffset += (size + 63) & ~63u;
}

//-----------------------------------------------------------------------------
// Deferred deletion
//-----------------------------------------------------------------------------

// Pure3D creates and frees textures and prim buffers on its loader threads. Those threads have shared contexts (see
// gldisplay.cpp) and delete at once; a thread without one queues the name for the render thread.
static pthread_mutex_t s_PendingDeleteLock = PTHREAD_MUTEX_INITIALIZER;
static GLuint s_PendingTextures[1024];
static int s_PendingTextureCount = 0;
static GLuint s_PendingBuffers[1024];
static int s_PendingBufferCount = 0;

static bool HaveGLContext()
{
    return SDL_GL_GetCurrentContext() != NULL;
}

void pglDeleteTexture(GLuint texture)
{
    if(!texture)
        return;
    if(HaveGLContext())
    {
        glDeleteTextures(1, &texture);
        return;
    }
    pthread_mutex_lock(&s_PendingDeleteLock);
    if(s_PendingTextureCount < 1024)
        s_PendingTextures[s_PendingTextureCount++] = texture;
    pthread_mutex_unlock(&s_PendingDeleteLock);
}

void pglDeleteBuffer(GLuint buffer)
{
    if(!buffer)
        return;
    if(HaveGLContext())
    {
        glDeleteBuffers(1, &buffer);
        return;
    }
    pthread_mutex_lock(&s_PendingDeleteLock);
    if(s_PendingBufferCount < 1024)
        s_PendingBuffers[s_PendingBufferCount++] = buffer;
    pthread_mutex_unlock(&s_PendingDeleteLock);
}

void pglFlushPendingDeletes()
{
    pthread_mutex_lock(&s_PendingDeleteLock);
    if(s_PendingTextureCount)
    {
        glDeleteTextures(s_PendingTextureCount, s_PendingTextures);
        s_PendingTextureCount = 0;
    }
    if(s_PendingBufferCount)
    {
        glDeleteBuffers(s_PendingBufferCount, s_PendingBuffers);
        s_PendingBufferCount = 0;
    }
    pthread_mutex_unlock(&s_PendingDeleteLock);
}
