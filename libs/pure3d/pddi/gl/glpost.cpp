//=============================================================================
// Linux port: user post-processing shaders (see glpost.hpp, shaders/README.md).
//=============================================================================

#include <pddi/gl/glpost.hpp>
#include <pddi/gl/glpipeline.hpp>
#include <pddi/gl/glcustom.hpp>

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <png.h>

//-----------------------------------------------------------------------------
// Saving and restoring the state a screen pass touches
//-----------------------------------------------------------------------------

static const GLenum s_passCaps[5] = { GL_DEPTH_TEST, GL_STENCIL_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST };

void pglScreenPassState::Save()
{
    for(int i = 0; i < 5; i++)
    {
        enabled[i] = glIsEnabled(s_passCaps[i]);
        glDisable(s_passCaps[i]);
    }
    glGetBooleanv(GL_COLOR_WRITEMASK, colourMask);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    for(int i = 0; i < 3; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void pglScreenPassState::Restore()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    for(int i = 2; i >= 0; i--)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    glActiveTexture(activeTexture);
    glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glColorMask(colourMask[0], colourMask[1], colourMask[2], colourMask[3]);
    for(int i = 0; i < 5; i++)
    {
        if(enabled[i])
            glEnable(s_passCaps[i]);
    }
}

//-----------------------------------------------------------------------------
// Passes
//-----------------------------------------------------------------------------

// one triangle covering the screen, made from the vertex index (no vertex data)
static const char* s_passVertexSource =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main()\n"
    "{\n"
    "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "    vUV = p * 0.5 + 0.5;\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// goes before every user file (which therefore has no #version line); #line 1 makes the driver's error messages
// count lines in the user's file
static const char* s_passHeader =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 fragColour;\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uOriginal;\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2 uResolution;\n"
    "uniform float uTime;\n"
    "uniform int uFrame;\n"
    "uniform float uNear;\n"
    "uniform float uFar;\n"
    "uniform mat4 uProjection;\n"
    "uniform mat4 uInverseProjection;\n"
    "// the eye-space position of what is drawn at uv (the camera at 0,0,0 looking down -z; GL's eye space)\n"
    "vec3 ViewPosition(vec2 uv)\n"
    "{\n"
    "    vec4 p = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r * 2.0 - 1.0, 1.0);\n"
    "    return p.xyz / p.w;\n"
    "}\n"
    "float LinearDepth(vec2 uv)\n"
    "{\n"
    "    float d = texture(uDepth, uv).r;\n"
    "    if(uFar <= 0.0)\n"
    "        return d;\n"
    "    float z = d * 2.0 - 1.0;\n"
    "    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));\n"
    "}\n"
    "#line 1\n";

const int MAX_PASSES = 16;
const int MAX_PASS_TEXTURES = 4;         // "#pragma shar_texture <sampler> <file.png>" per pass, on units 3 and up

// images passes ask for, loaded once per path (and again when the file changes)
struct Image
{
    char path[600];
    time_t mtime;
    GLuint texture;             // 0: could not be read
};
static Image s_images[MAX_PASSES * 2 * MAX_PASS_TEXTURES];
static int s_imageCount;

struct Pass
{
    char file[256];             // as listed in post.ini (relative to the shaders folder)
    time_t mtime;
    pglProgram program;
    bool ok;
    bool beginsEffect;          // "#pragma shar_original": uOriginal is this pass's input from here on
    float scale;                // "#pragma shar_scale <f>": draws at f times the picture's size (not the last pass)
    GLuint texture, framebuffer;    // its own target when scaled (kept in the slot, resized as needed)
    int texW, texH;
    int textureCount;
    GLint textureUniform[MAX_PASS_TEXTURES];
    int textureImage[MAX_PASS_TEXTURES];    // indices in s_images
    GLint uScene, uOriginal, uDepth, uResolution, uTime, uFrame, uNear, uFar, uProjection, uInverseProjection;
};

static Pass s_passes[PGL_POST_STAGES][MAX_PASSES];
static int s_passCount[PGL_POST_STAGES];
static char s_folder[512];
static time_t s_iniTime;
static Uint32 s_lastCheck;
static int s_state = 0;         // 0 not set up, 1 set up, -1 off (SHAR_POST=0)

static GLuint s_vao, s_original, s_pingPong[2], s_framebuffers[3], s_depth;     // framebuffer 2: s_original
static int s_texW, s_texH;
static bool s_depthBroken;

static time_t FileTime(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

static char* ReadFile(const char* path)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc(size + 1);
    if(text)
    {
        size = (long)fread(text, 1, size, f);
        text[size] = 0;
    }
    fclose(f);
    return text;
}

static void PngError(png_structp png, png_const_charp message)
{
    fprintf(stderr, "PDDI: post-processing: PNG error: %s\n", message);
    longjmp(*(jmp_buf*)png_get_error_ptr(png), 1);
}

static void PngWarning(png_structp, png_const_charp)
{
}

// a PNG as an RGBA8 texture (rows as stored: the image's top row at v = 0), linear filtering, clamped; 0 on failure
static GLuint LoadPng(const char* path)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return 0;
    jmp_buf failed;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &failed, PngError, PngWarning);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    unsigned char* pixels = NULL;
    png_bytep* rows = NULL;
    GLuint texture = 0;
    if(png && info && setjmp(failed) == 0)
    {
        png_init_io(png, f);
        png_read_info(png, info);
        int depth = png_get_bit_depth(png, info);
        int type = png_get_color_type(png, info);
        if(type == PNG_COLOR_TYPE_PALETTE || depth < 8 || png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_expand(png);
        if(type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png);
        if(depth == 16)
            png_set_strip_16(png);
        if(!(type & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_filler(png, 0xff, PNG_FILLER_AFTER);
        png_set_interlace_handling(png);
        png_read_update_info(png, info);

        int w = png_get_image_width(png, info);
        int h = png_get_image_height(png, info);
        pixels = (unsigned char*)malloc((size_t)w * h * 4);
        rows = (png_bytep*)malloc(h * sizeof(png_bytep));
        if(pixels && rows && png_get_rowbytes(png, info) == (png_uint_32)w * 4)
        {
            for(int y = 0; y < h; y++)
                rows[y] = pixels + (size_t)y * w * 4;
            png_read_image(png, rows);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    if(png)
        png_destroy_read_struct(&png, info ? &info : NULL, NULL);
    free(rows);
    free(pixels);
    fclose(f);
    return texture;
}

// the image's index in s_images, loading it the first time (or when the file changed); file is relative to the
// folder of the pass that asks for it (passFile, relative to the shaders folder)
static int UseImage(const char* passFile, const char* file)
{
    char folder[300];
    snprintf(folder, sizeof(folder), "%s", passFile);
    char* slash = strrchr(folder, '/');
    if(slash)
        slash[1] = 0;
    else
        folder[0] = 0;
    char path[600];
    snprintf(path, sizeof(path), "%s/%s%s", s_folder, folder, file);
    time_t mtime = FileTime(path);
    for(int i = 0; i < s_imageCount; i++)
    {
        Image& image = s_images[i];
        if(strcmp(image.path, path) != 0)
            continue;
        if(image.mtime != mtime)
        {
            if(image.texture)
                glDeleteTextures(1, &image.texture);
            image.texture = LoadPng(path);
            image.mtime = mtime;
        }
        return i;
    }
    if(s_imageCount == (int)(sizeof(s_images) / sizeof(s_images[0])))
        return -1;
    Image& image = s_images[s_imageCount];
    snprintf(image.path, sizeof(image.path), "%s", path);
    image.mtime = mtime;
    image.texture = LoadPng(path);
    if(!image.texture)
        fprintf(stderr, "PDDI: post-processing: can't load %s (PNG)\n", path);
    return s_imageCount++;
}

// "#pragma shar_texture <sampler> <file>" lines: the pass's images
static void FindTextures(Pass& p, const char* source)
{
    p.textureCount = 0;
    for(const char* s = strstr(source, "#pragma shar_texture"); s && p.textureCount < MAX_PASS_TEXTURES;
        s = strstr(s + 1, "#pragma shar_texture"))
    {
        char sampler[64], file[256];
        if(sscanf(s + strlen("#pragma shar_texture"), " %63s %255s", sampler, file) != 2)
            continue;
        int image = UseImage(p.file, file);
        if(image < 0)
            continue;
        glUseProgram(p.program.Handle());
        p.textureUniform[p.textureCount] = p.program.Uniform(sampler);
        glUniform1i(p.textureUniform[p.textureCount], 3 + p.textureCount);
        p.textureImage[p.textureCount] = image;
        p.textureCount++;
    }
    glUseProgram(0);
}

static void BuildPass(Pass& p)
{
    char path[800];
    snprintf(path, sizeof(path), "%s/%s", s_folder, p.file);
    p.mtime = FileTime(path);
    p.program.Release();
    p.ok = false;
    size_t length = strlen(p.file);
    if(length > 4 && strcmp(p.file + length - 4, ".spv") == 0)
    {
        fprintf(stderr, "PDDI: post-processing: %s is SPIR-V, for the Vulkan renderer only: skipped\n", p.file);
        return;
    }

    char* source = ReadFile(path);
    if(!source)
    {
        fprintf(stderr, "PDDI: post-processing: can't read %s\n", path);
        return;
    }
    // each "#pragma shar_texture <sampler> <file>" declares its sampler: the header gets the declarations, before the
    // user's line 1
    char header[4096];
    snprintf(header, sizeof(header), "%s", s_passHeader);
    char* lineMark = strstr(header, "#line 1\n");
    if(lineMark)
        *lineMark = 0;
    for(const char* t = strstr(source, "#pragma shar_texture"); t; t = strstr(t + 1, "#pragma shar_texture"))
    {
        char sampler[64];
        if(sscanf(t + strlen("#pragma shar_texture"), " %63s", sampler) == 1 && strlen(header) + 100 < sizeof(header))
            snprintf(header + strlen(header), sizeof(header) - strlen(header), "uniform sampler2D %s;\n", sampler);
    }
    snprintf(header + strlen(header), sizeof(header) - strlen(header), "#line 1\n");
    p.ok = p.program.Build(path, s_passVertexSource, source, NULL, header);
    p.beginsEffect = strstr(source, "#pragma shar_original") != NULL;
    p.scale = 1.0f;
    const char* scale = strstr(source, "#pragma shar_scale");
    if(scale && sscanf(scale + strlen("#pragma shar_scale"), " %f", &p.scale) == 1)
        p.scale = p.scale < 0.1f ? 0.1f : p.scale > 1.0f ? 1.0f : p.scale;
    else
        p.scale = 1.0f;
    p.textureCount = 0;
    if(p.ok)
        FindTextures(p, source);
    free(source);
    if(!p.ok)
        return;

    glUseProgram(p.program.Handle());
    p.uScene = p.program.Uniform("uScene");
    p.uOriginal = p.program.Uniform("uOriginal");
    p.uDepth = p.program.Uniform("uDepth");
    p.uResolution = p.program.Uniform("uResolution");
    p.uTime = p.program.Uniform("uTime");
    p.uFrame = p.program.Uniform("uFrame");
    p.uNear = p.program.Uniform("uNear");
    p.uFar = p.program.Uniform("uFar");
    p.uProjection = p.program.Uniform("uProjection");
    p.uInverseProjection = p.program.Uniform("uInverseProjection");
    glUniform1i(p.uScene, 0);
    glUniform1i(p.uOriginal, 1);
    glUniform1i(p.uDepth, 2);
    glUseProgram(0);
}

// post.ini: "[scene]" and "[final]" sections, one shader file per line (relative to the shaders folder), run in
// order; '#' or ';' starts a comment
static void LoadConfig()
{
    for(int s = 0; s < PGL_POST_STAGES; s++)
    {
        for(int i = 0; i < s_passCount[s]; i++)
            s_passes[s][i].program.Release();
        s_passCount[s] = 0;
    }

    char path[600];
    snprintf(path, sizeof(path), "%s/post.ini", s_folder);
    s_iniTime = FileTime(path);
    char* text = ReadFile(path);
    if(!text)
        return;

    int stage = -1;
    for(char* line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n"))
    {
        char* c = strpbrk(line, "#;");
        if(c)
            *c = 0;
        while(*line == ' ' || *line == '\t')
            line++;
        char* end = line + strlen(line);
        while(end > line && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if(!*line)
            continue;
        if(strcmp(line, "[scene]") == 0)
            stage = PGL_POST_SCENE;
        else if(strcmp(line, "[final]") == 0)
            stage = PGL_POST_FINAL;
        else if(line[0] == '[')
        {
            fprintf(stderr, "PDDI: post-processing: unknown section %s in %s\n", line, path);
            stage = -1;
        }
        else if(stage < 0)
            fprintf(stderr, "PDDI: post-processing: %s is outside [scene] or [final] in %s\n", line, path);
        else if(s_passCount[stage] == MAX_PASSES)
            fprintf(stderr, "PDDI: post-processing: more than %d passes in a stage, %s left out\n", MAX_PASSES, line);
        else
        {
            Pass& p = s_passes[stage][s_passCount[stage]++];
            snprintf(p.file, sizeof(p.file), "%s", line);
            BuildPass(p);
        }
    }
    free(text);

    for(int s = 0; s < PGL_POST_STAGES; s++)
    {
        if(!s_passCount[s])
            continue;
        fprintf(stderr, "PDDI: post-processing, %s stage:", s == PGL_POST_SCENE ? "scene" : "final");
        for(int i = 0; i < s_passCount[s]; i++)
            fprintf(stderr, " %s%s", s_passes[s][i].file, s_passes[s][i].ok ? "" : " (failed)");
        fprintf(stderr, "\n");
    }
}

// once a second: post.ini or a shader changed -> reload (a shader that no longer builds is skipped until fixed)
static void CheckForChanges()
{
    Uint32 now = SDL_GetTicks();
    if(s_state == 1 && now - s_lastCheck < 1000)
        return;
    s_lastCheck = now;

    if(s_state == 0)
    {
        const char* e = getenv("SHAR_POST");
        if(e && e[0] == '0')
        {
            s_state = -1;
            return;
        }
        const char* folder = getenv("SHAR_SHADERS");
        snprintf(s_folder, sizeof(s_folder), "%s", folder && *folder ? folder : "shaders");
        s_state = 1;
        LoadConfig();
        return;
    }

    char path[600];
    snprintf(path, sizeof(path), "%s/post.ini", s_folder);
    if(FileTime(path) != s_iniTime)
    {
        LoadConfig();
        return;
    }
    for(int s = 0; s < PGL_POST_STAGES; s++)
    {
        for(int i = 0; i < s_passCount[s]; i++)
        {
            Pass& p = s_passes[s][i];
            char file[800];
            snprintf(file, sizeof(file), "%s/%s", s_folder, p.file);
            if(FileTime(file) != p.mtime)
            {
                BuildPass(p);
                fprintf(stderr, "PDDI: post-processing: reloaded %s%s\n", p.file, p.ok ? "" : " (failed)");
            }
        }
    }
}

// a general 4x4 inverse (column-major), for the projection; identity when it is singular
static void Invert(const float* m, float* out)
{
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if(det == 0.0f)
    {
        memset(out, 0, 16 * sizeof(float));
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    for(int i = 0; i < 16; i++)
        out[i] = inv[i] / det;
}

static void SetupTexture(GLuint texture, GLint internalFormat, GLenum format, GLenum type, GLint filter, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void SetupTargets(int w, int h)
{
    if(!s_vao)
    {
        glGenVertexArrays(1, &s_vao);
        glGenTextures(1, &s_original);
        glGenTextures(2, s_pingPong);
        glGenTextures(1, &s_depth);
        glGenFramebuffers(3, s_framebuffers);
    }
    if(w == s_texW && h == s_texH)
        return;
    s_texW = w;
    s_texH = h;
    SetupTexture(s_original, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, w, h);
    SetupTexture(s_depth, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST, w, h);
    for(int i = 0; i < 2; i++)
    {
        SetupTexture(s_pingPong[i], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, w, h);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_framebuffers[i]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_pingPong[i], 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_framebuffers[2]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_original, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void pglPostRun(pglPostStage stage, int w, int h, pglPipeline* pipeline)
{
    CheckForChanges();
    if(s_state != 1 || !g_pglUserShaders || w <= 0 || h <= 0)
        return;

    Pass* passes[MAX_PASSES];
    int n = 0;
    bool needDepth = false;
    for(int i = 0; i < s_passCount[stage]; i++)
    {
        if(!s_passes[stage][i].ok)
            continue;
        passes[n++] = &s_passes[stage][i];
        needDepth |= s_passes[stage][i].uDepth >= 0;
    }
    if(n == 0)
        return;

    pglScreenPassState saved;
    saved.Save();
    SetupTargets(w, h);

    // the frame so far, and the depth buffer if a pass reads it (copying into a depth texture reads depth)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_original);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_depth);
    if(needDepth && !s_depthBroken)
    {
        while(glGetError() != GL_NO_ERROR) {}
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        if(glGetError() != GL_NO_ERROR)
        {
            fprintf(stderr, "PDDI: post-processing: the depth buffer can't be copied: uDepth reads 0\n");
            s_depthBroken = true;
        }
    }

    // near and far planes of the last perspective projection (0 when it was not one), the projection and its inverse
    float nearPlane = 0.0f, farPlane = 0.0f;
    const float* m = pipeline->GetProjection();
    if(m[11] == -1.0f && m[10] != 1.0f && m[10] != -1.0f)
    {
        nearPlane = m[14] / (m[10] - 1.0f);
        farPlane = m[14] / (m[10] + 1.0f);
    }
    float inverse[16];
    Invert(m, inverse);

    static int s_frame = 0;
    s_frame++;
    float time = SDL_GetTicks() / 1000.0f;

    glBindVertexArray(s_vao);
    GLuint input = s_original;
    GLuint inputFramebuffer = s_framebuffers[2];
    int inputW = w, inputH = h;
    for(int i = 0; i < n; i++)
    {
        Pass& p = *passes[i];
        bool last = i == n - 1;

        // where the pass draws: the frame (last pass), its own smaller target (scaled), or a ping-pong texture
        GLuint output = s_pingPong[i & 1], framebuffer = last ? saved.drawFramebuffer : s_framebuffers[i & 1];
        int outW = w, outH = h;
        if(!last && p.scale < 1.0f)
        {
            outW = (int)(w * p.scale + 0.5f);
            outH = (int)(h * p.scale + 0.5f);
            outW = outW < 1 ? 1 : outW;
            outH = outH < 1 ? 1 : outH;
            if(!p.texture)
            {
                glGenTextures(1, &p.texture);
                glGenFramebuffers(1, &p.framebuffer);
            }
            if(p.texW != outW || p.texH != outH)
            {
                p.texW = outW;
                p.texH = outH;
                SetupTexture(p.texture, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, outW, outH);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p.framebuffer);
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.texture, 0);
            }
            output = p.texture;
            framebuffer = p.framebuffer;
        }

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, outW, outH);
        glUseProgram(p.program.Handle());
        glUniform2f(p.uResolution, (float)outW, (float)outH);
        glUniform1f(p.uTime, time);
        glUniform1i(p.uFrame, s_frame);
        glUniform1f(p.uNear, nearPlane);
        glUniform1f(p.uFar, farPlane);
        glUniformMatrix4fv(p.uProjection, 1, GL_FALSE, m);
        glUniformMatrix4fv(p.uInverseProjection, 1, GL_FALSE, inverse);

        // a pass that begins an effect makes its input the effect's uOriginal (copied: a ping-pong texture is
        // written again two passes on)
        if(p.beginsEffect && input != s_original)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, inputFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_framebuffers[2]);
            glBlitFramebuffer(0, 0, inputW, inputH, 0, 0, w, h, GL_COLOR_BUFFER_BIT,
                              inputW == w && inputH == h ? GL_NEAREST : GL_LINEAR);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        }
        for(int t = 0; t < p.textureCount; t++)
        {
            glActiveTexture(GL_TEXTURE3 + t);
            glBindTexture(GL_TEXTURE_2D, s_images[p.textureImage[t]].texture);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        input = output;
        inputFramebuffer = framebuffer;
        inputW = outW;
        inputH = outH;
    }
    glBindVertexArray(0);
    glUseProgram(0);

    saved.Restore();
    pipeline->Unbind();
}
