//=============================================================================
// Linux port: OpenGL 3.3 core renderer.
//=============================================================================

// The GL pddi was written for fixed-function OpenGL 1.x (built-in lights, materials, fog, matrix stacks, client-side
// vertex arrays, glBegin/glEnd). In a core profile none of that exists: pglPipeline keeps the same state on the CPU
// and draws through GLSL programs that compute what Direct3D 8's fixed-function pipeline computed with the settings
// the D3D8 pddi (the one the game shipped with) used: per-vertex lighting, texture stages, linear fog, alpha test.
// There is one program per kind of pddi shader (pglProgramType); pglProgram is the general compile/link wrapper.

#ifndef _GLPIPELINE_HPP_
#define _GLPIPELINE_HPP_

#include <pddi/gl/display_linux/gl.hpp>
#include <pddi/gl/glshaders.hpp>

class pglProgram
{
public:
    pglProgram();
    ~pglProgram();

    // compiles and links; on failure prints the driver's log (prefixed with name) and returns false. header, if
    // given, goes before both sources (then they leave out the #version line); fragmentHeader, if given, goes before
    // the fragment source instead.
    bool Build(const char* name, const char* vertexSource, const char* fragmentSource, const char* header = 0,
               const char* fragmentHeader = 0);
    void Release();         // deletes the program (render thread)
    GLint Uniform(const char* name) const;
    GLuint Handle() const { return program; }

private:
    GLuint program;
};

// vertex attribute locations shared by every program
enum
{
    PGL_ATTRIB_POSITION = 0,
    PGL_ATTRIB_NORMAL = 1,
    PGL_ATTRIB_COLOUR = 2,
    PGL_ATTRIB_UV = 3
};

// one vertex of an immediate-mode stream (glBegin/glEnd replacement)
struct pglVertex
{
    float position[3];
    float normal[3];
    float uv[2];
    unsigned char colour[4];
};

const int pglMaxLights = 8;

const int pglMaxCustomPrograms = 32;       // user material shaders (glcustom.cpp)

// which faces a material draws (pglContext::SetMaterialCull): as the game says, or all (two-sided materials, a custom
// shader's "#pragma shar_cull none"). The game itself draws with culling off (RenderFlow) and its art's winding is not
// consistent, so there is no "reverse": shaders tell faces apart by their normals instead.
enum { PGL_CULL_GAME, PGL_CULL_NONE };


class pglPipeline
{
public:
    // what the material sets for a draw (pglMat::SetDevPass); textures are bound to units 0 and 1 by the material
    struct Material
    {
        int program;                // pglProgramType
        int custom;                 // a custom material shader (BuildCustomProgram's slot) to draw with, or -1
        bool textured;
        bool reflectionTextured;    // a reflection map on unit 1
        bool lit;
        int alphaCompare;           // pddiCompareMode, or -1 for no alpha test
        float alphaRef;
        bool blended;               // blending on (an alpha-tested material then keeps its plain alpha test)
        float ambient[4];
        float diffuse[4];
        float specular[4];
        float emissive[4];
        float shininess;            // 0: no specular, as D3D8 (SPECULARENABLE = shininess > 0)
        float envBlend[4];          // reflection map multiplier
        float refractColour[4];     // refraction: multiplier of the screen behind
        float refractIndex;         // refraction: offset by the eye-space normal, in pixels of a 448 line screen
        float refractBlend;         // refraction: 1 the screen behind only, 0 the material only
    };

    pglPipeline();
    ~pglPipeline();

    // GL objects; render thread, with the context current. False if the shaders do not build.
    bool Init();

    // matrices (column-major, as glLoadMatrixf took them)
    void SetProjection(const float* m);
    const float* GetProjection() const { return projection; }
    void SetModelView(const float* m);
    void LoadIdentity();            // both, as BeginFrame did

    // lights: position (w = 0: direction towards the light) in the coordinates of the model-view loaded now, which
    // transforms it to eye space as glLightfv(GL_POSITION) did
    void SetLight(int i, bool enabled, const float* position, const float* colour, float attenA, float attenB, float attenC,
                  float range);
    void SetAmbient(const float* colour);

    void EnableFog(bool enable);
    void SetFog(const float* colour, float start, float end);

    void SetMaterial(const Material& m);

    // the "current" vertex values (glColor, glNormal, glTexCoord): used for attributes a vertex does not carry
    pglVertex current;

    // immediate mode: BeginStream, then set current values and Emit vertices, then EndStream draws them
    void BeginStream(GLenum primType);
    void Emit(float x, float y, float z);
    void EndStream();

    // user material shaders: ShadeMaterial() in GLSL (shaders/README.md). Returns the slot for Material::custom,
    // or -1 when it does not build (the driver's log is printed). Release drops them all (materials' slots with them).
    int BuildCustomProgram(const char* name, const char* source);
    int CustomCull(int slot) const;     // the slot's PGL_CULL_* directive
    void ReleaseCustomPrograms();
    int CustomProgramCount() const { return customCount; }

    // from BeginFrame: the time and frame size (uTime, uResolution) custom shaders see
    void NewFrame(int width, int height);

    // before drawing anything: binds the material's program and the vertex array object, uploads changed uniforms
    void Prepare();

    // the framebuffer drawn to is multisampled (MSAA, gldisplay.cpp): alpha-tested materials that do not blend
    // then use alpha-to-coverage, which smooths their cut-out edges (fences, foliage)
    void SetMultisampled(bool multisampled);

    // texture filtering settings (gldisplay.cpp pglSetTextureFiltering): with mipmaps, alpha-tested textures that do
    // not blend have their alpha raised with distance so they keep their coverage (fences, foliage would thin out)
    void SetTextureFiltering(bool mipmaps, float anisotropy);

    // refraction: copies the frame drawn so far, on the frame's first call (later refracting draws see the same copy,
    // as on the PS2) or the first after pglRecaptureScreen, and binds the copy to texture unit 2
    void CaptureScreen();

    // after GL calls outside the pipeline that change the program or vertex array object (the gamma pass)
    void Unbind() { bound = 0; }

    // for a draw without an array for attrib (prim buffers): use the current value instead
    void UseCurrent(GLuint attrib);

    // SHAR_GL_STATS=1: per-frame averages of draws, uniform and buffer uploads, printed every 5 s (from BeginFrame)
    void FrameStats();
    void FrameEnd();            // from EndFrame: GPU and CPU time of the frame's rendering
    struct Stats { unsigned frames, draws, streamDraws, matrixUploads, lightUploads, materialUploads, bufferUploads, bufferBytes;
                   double gpuMs, gpuMaxMs, cpuMs; unsigned gpuFrames; } stats;

private:
    // state groups; each program keeps the version of each it last received
    enum { GROUP_MATRICES, GROUP_LIGHTS, GROUP_FOG, GROUP_MATERIAL, GROUP_COUNT };

    struct Program
    {
        pglProgram program;
        GLint uniform[U_COUNT];
        unsigned uploaded[GROUP_COUNT];
        unsigned timeFrame;         // frame whose time and size uTime, uResolution hold
        int cull;                   // PGL_CULL_* (custom programs)
    };

    void Changed(int group) { version[group]++; }
    bool AlphaToCoverage() const;
    void UploadUniforms(Program& p);
    bool BuildProgram(Program& p, const char* name, const char* header, const char* fragmentBody, bool custom,
                      const char* fragmentHeader, const char* vertexBody);

    bool ready;
    bool multisampled;
    float mipAlpha;             // the anisotropy when mipmapping is on, else 0
    int alphaToCoverageState;   // GL_SAMPLE_ALPHA_TO_COVERAGE as last set, -1 unknown
    Program programs[PGL_PROGRAM_COUNT + pglMaxCustomPrograms];   // built-in ones, then custom ones
    int customCount;
    unsigned frameCount;
    float frameTime, frameWidth, frameHeight;
    Program* bound;             // the program in use, NULL when unknown
    unsigned version[GROUP_COUNT];
    GLuint vao;

    // CaptureScreen's copy
    GLuint screenTexture, screenFbo;
    int screenWidth, screenHeight;
    unsigned screenFrame;

    // streamed vertices: CPU staging, then a ring buffer on the GPU
    pglVertex* streamVerts;
    int streamCount, streamCapacity;
    GLenum streamPrim;
    GLuint streamBuffer;
    unsigned streamBufferSize, streamOffset;

    float projection[16];
    float modelView[16];

    struct Light
    {
        bool enabled;
        float position[4];      // eye space
        float colour[3];
        float atten[3];
        float range;
    } lights[pglMaxLights];
    float ambient[4];

    bool fogEnabled;
    float fogColour[3];
    float fogStart, fogEnd;

    Material material;
};

// GL object deletion from any thread: deleted at once where a context is current, else queued for the render thread
void pglDeleteTexture(GLuint texture);
void pglDeleteBuffer(GLuint buffer);
void pglFlushPendingDeletes();      // render thread

// the next refracting draw copies the frame again (HoverCarRefraction's per car screen capture, geometryvehicle.cpp)
void pglRecaptureScreen();

#endif
