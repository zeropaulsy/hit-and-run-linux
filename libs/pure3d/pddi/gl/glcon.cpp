//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================




#if defined(RAD_WIN32) && !defined(RAD_LINUX)
    #include <pddi/gl/display_win32/gldisplay.hpp>
    #include <pddi/gl/display_win32/gl.hpp>
#endif

#ifdef RAD_LINUX
    #include <pddi/gl/display_linux/gldisplay.hpp>
    #include <pddi/gl/display_linux/gl.hpp>
#endif


#include <pddi/gl/glcon.hpp>
#include <pddi/gl/gldev.hpp>
#include <pddi/gl/gltex.hpp>
#include <pddi/gl/glmat.hpp>
#include <pddi/gl/glcustom.hpp>
#include <pddi/gl/glpipeline.hpp>

#include <pddi/base/debug.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>

// vertex arrays rendering
GLenum primTypeTable[5] =
{
    GL_TRIANGLES, //PDDI_PRIM_TRIANGLES
    GL_TRIANGLE_STRIP, //PDDI_PRIM_TRISTRIP
    GL_LINES, //PDDI_PRIM_LINES
    GL_LINE_STRIP, // PDDI_PRIM_LINESTRIP
    GL_POINTS, //PDDI_PRIM_POINTS
};

static inline void FillGLColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}


// extensions
class pglExtContext : public pddiExtGLContext 
{
public:
    pglExtContext(pglDisplay* d) : display(d) {}

    void BeginContext() {display->BeginContext();}
    void EndContext() {display->EndContext();}

private:
    pglDisplay* display;
};

class pglExtGamma : public pddiExtGammaControl
{
public:
    pglExtGamma(pglDisplay* d) { display = d;}

    void SetGamma(float r, float g, float b)     {display->SetGamma(r,g,b);}
    void GetGamma(float *r, float *g, float *b)  {display->GetGamma(r,g,b);}

protected:
    pglDisplay* display;
};

pglContext::pglContext(pglDevice* dev, pglDisplay* disp) : pddiBaseContext((pddiDisplay*)disp,(pddiDevice*)dev)
{
    device = dev;
    display = disp;
    pipeline = disp->GetPipeline();
    materialCull = 0;      // PGL_CULL_GAME

    device->AddRef();
    display->AddRef();
    disp->SetContext(this);

    DefaultState();
    contextID = 0;

    extContext = new pglExtContext(display);
    extGamma = new pglExtGamma(display);

    defaultShader = new pglMat(this);
    defaultShader->AddRef();
}

pglContext::~pglContext()
{
    defaultShader->Release();

    delete extContext;
    delete extGamma;

    display->SetContext(NULL);
    display->Release();
    device->Release();
}

// frame synchronisation
void pglContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();

    if(display->HasReset())
    {
        contextID++;

        // Linux port: the perspective hint, vertex array client state, current colour and separate specular
        // colour of the fixed-function setup are the pipeline's (glpipeline.cpp)
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        pipeline->current.colour[0] = pipeline->current.colour[1] = pipeline->current.colour[2] = pipeline->current.colour[3] = 255;

        glEnable(GL_DITHER);

        SyncState(0xffffffff);
    }

    pglFlushPendingDeletes();
    pipeline->NewFrame(display->GetWidth(), display->GetHeight());
    pglCustomUpdate(pipeline);      // Linux port: user material shaders (glcustom.cpp)
    pipeline->LoadIdentity();
    pipeline->FrameStats();
    display->BeginFrameTarget();    // Linux port: the multisampled framebuffer, when anti-aliasing is on
}

void pglContext::EndFrame()
{
     pddiBaseContext::EndFrame();
     pipeline->FrameEnd();
}

// buffer clearing
void pglContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);

    int myClearMask = 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_COLOUR) ? GL_COLOR_BUFFER_BIT : 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_DEPTH) ? GL_DEPTH_BUFFER_BIT : 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_STENCIL) ? GL_STENCIL_BUFFER_BIT : 0;

    glClearDepth(state.viewState->clearDepth);
    glClearColor(float(state.viewState->clearColour.Red())/255.0f, 
                     float(state.viewState->clearColour.Green())/255.0f, 
                     float(state.viewState->clearColour.Blue())/255.0f,
                     float(state.viewState->clearColour.Alpha())/255.0f);
    glClearStencil(state.viewState->clearStencil);

    // Linux port: Direct3D's Clear ignores the write masks; glClear honours them, so a clear while colour,
    // depth or stencil writes are off silently did nothing. Open the masks for the clear, then restore them.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(~0u);
    glClear(myClearMask);
    glColorMask(state.renderState->redWrite, state.renderState->greenWrite,
                state.renderState->blueWrite, state.renderState->alphaWrite);
    glDepthMask(state.renderState->zWrite);
    glStencilMask(state.stencilState->writeMask);
}

// Linux port: glOrtho and glFrustum (applied to an identity matrix), column-major
static void Ortho(float* m, float l, float r, float b, float t, float n, float f)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

static void Frustum(float* m, float l, float r, float b, float t, float n, float f)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f * n / (r - l);
    m[5] = 2.0f * n / (t - b);
    m[8] = (r + l) / (r - l);
    m[9] = (t + b) / (t - b);
    m[10] = -(f + n) / (f - n);
    m[11] = -1.0f;
    m[14] = -2.0f * f * n / (f - n);
}

void pglContext::SetupHardwareProjection(void)
{
    float m[16];
    switch(state.viewState->projectionMode)
    {
        case PDDI_PROJECTION_DEVICE :
            Ortho(m, 0, display->GetWidth(),
                      display->GetHeight(), 0,
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            pipeline->SetProjection(m);
            glViewport(0, 0, display->GetWidth(), display->GetHeight());
            break;

        case PDDI_PROJECTION_ORTHOGRAPHIC :
            Ortho(m, -0.5,  0.5,
                      -((1/state.viewState->camera.aspect)/2),  ((1/state.viewState->camera.aspect)/2),
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            pipeline->SetProjection(m);
            glViewport(int(state.viewState->viewWindow.left * display->GetWidth()), 
                              int((1.0f - state.viewState->viewWindow.bottom) * display->GetHeight() ),
                              int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth()), 
                              int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * display->GetHeight()));
            break;

        case PDDI_PROJECTION_PERSPECTIVE :
            {
                float halfX = (float)tan(double(state.viewState->camera.fov * 0.5)) * state.viewState->camera.nearPlane;
                float halfY = halfX / state.viewState->camera.aspect;

                Frustum(m, -halfX,halfX,-halfY,halfY,state.viewState->camera.nearPlane,state.viewState->camera.farPlane);
                pipeline->SetProjection(m);
                glViewport(int(state.viewState->viewWindow.left * display->GetWidth()),
                              int((1.0f - state.viewState->viewWindow.bottom) * display->GetHeight() ),
                              int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth()),
                              int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * display->GetHeight()));
            }
            break;
        default:
            PDDIASSERTMSG(0, "Bad projection mode","");
            break;
    }

}

void pglContext::LoadHardwareMatrix(pddiMatrixType id)
{
    switch(id)
    {
        case PDDI_MATRIX_MODELVIEW :
        {

            pddiMatrix tmp = *state.matrixStack[id]->Top();
            tmp.m[0][2] = -tmp.m[0][2];
            tmp.m[1][2] = -tmp.m[1][2];
            tmp.m[2][2] = -tmp.m[2][2];
            tmp.m[3][2] = -tmp.m[3][2];
            pipeline->SetModelView((float*)&tmp);
        }
        break;
        default :
            PDDIASSERTMSG(0, "Invalid matrix load","");
            break;
    }
}

// viewport clipping
void pglContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);
    if(!rect)
    {
        glDisable(GL_SCISSOR_TEST);
    }
    else
    {
        glScissor(rect->left, display->GetHeight() - rect->bottom, rect->right - rect->left, rect->bottom - rect->top);
        glEnable(GL_SCISSOR_TEST);
    }
}

// Linux port: immediate mode without glBegin/glEnd. As in OpenGL, Colour/Normal/UV set the current values and each
// coordinate emits a vertex with them; EndPrims draws the lot (glpipeline.cpp).
class pglPrimStream : public pddiPrimStream
{
public:
    pglPipeline* pipe;

    void SetColour(pddiColour c)
    {
        pipe->current.colour[0] = c.Red();
        pipe->current.colour[1] = c.Green();
        pipe->current.colour[2] = c.Blue();
        pipe->current.colour[3] = c.Alpha();
    }

    void SetNormal(float x, float y, float z)
    {
        pipe->current.normal[0] = x;
        pipe->current.normal[1] = y;
        pipe->current.normal[2] = z;
    }

    void SetUV(float u, float v)
    {
        pipe->current.uv[0] = u;
        pipe->current.uv[1] = v;
    }

    void Coord(float x, float y, float z)  
    { 
        pipe->Emit(x,y,z); 
    }

    void Normal(float x, float y, float z) 
    { 
        SetNormal(x,y,z); 
    }

    void Colour(pddiColour colour, int channel = 0)
    {        
        // HBW: Multiple CBVs not yet implemented.  For now just ignore channel.
        SetColour(colour); 
    }

    void UV(float u, float v, int channel = 0) 
    { 
        if(channel == 0)
        {
            SetUV(u,v); 
        }
    }

    void Specular(pddiColour colour) 
    {
        //
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        SetColour(c);
        pipe->Emit(v->x,v->y,v->z);
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        SetNormal(n->x,n->y,n->z);
        pipe->Emit(v->x,v->y,v->z);
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        SetUV(uv->u, uv->v);
        pipe->Emit(v->x,v->y,v->z);
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        SetColour(c);
        SetUV(uv->u, uv->v);
        pipe->Emit(v->x,v->y,v->z);
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        SetNormal(n->x,n->y,n->z);
        SetUV(uv->u, uv->v);
        pipe->Emit(v->x,v->y,v->z);
    }

} thePrimStream;

pddiPrimStream* pglContext::BeginPrims(pddiShader* mat, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    if(!mat)
        mat = defaultShader;

    pddiBaseContext::BeginPrims(mat, primType, vertexType, vertexCount);
    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    material->SetMaterial();
    thePrimStream.pipe = pipeline;
    pipeline->BeginStream(primTypeTable[primType]);
    return &thePrimStream;
}

void pglContext::EndPrims(pddiPrimStream* stream)
{
    pddiBaseContext::EndPrims(stream);
    pipeline->EndStream();
}


class pglPrimBufferStream : public pddiPrimBufferStream
{
public:
    pglPrimBuffer* buffer;

    pglPrimBufferStream(pglPrimBuffer* b)
    {
        buffer = b;
    }

    void Next(void)  
    {
        if(buffer->coord)
            buffer->coord += 3;

        if(buffer->normal)
            buffer->normal += 3;

        if(buffer->uv)
            buffer->uv += 2;

        if(buffer->colour)
            buffer->colour += 4;

        buffer->total++;
        PDDIASSERT(buffer->total <= buffer->allocated);
        if(buffer->total >= buffer->allocated)
        {
            // Linux port: past the allocation, further vertices overwrite the last one instead of the heap
            // (Unlock reports them); pddidx8 would have locked no more than its vertex buffer either
            unsigned last = buffer->allocated > 0 ? buffer->allocated - 1 : 0;
            if(buffer->coord)  buffer->coord = buffer->coordBase + last * 3;
            if(buffer->normal) buffer->normal = buffer->normalBase + last * 3;
            if(buffer->uv)     buffer->uv = buffer->uvBase + last * 2;
            if(buffer->colour) buffer->colour = buffer->colourBase + last * 4;
        }
    }

    void Position(float x, float y, float z)  
    { 
        buffer->coord[0] = x;
        buffer->coord[1] = y;
        buffer->coord[2] = z;
        Next();
    }

    void Normal(float x, float y, float z) 
    { 
        buffer->normal[0] = x;
        buffer->normal[1] = y;
        buffer->normal[2] = z;
    }

    void Colour(pddiColour colour, int channel = 0)         
    {
        // HBW: Multiple CBVs not yet implemented.  For now just ignore channel.
        buffer->colour[0] = colour.Red();
        buffer->colour[1] = colour.Green();
        buffer->colour[2] = colour.Blue();
        buffer->colour[3] = colour.Alpha();
    }

    void TexCoord1(float u, int channel = 0) {}

    void TexCoord2(float u, float v, int channel = 0) 
    { 
        if(channel == 0)
        {
            buffer->uv[0] = u;
            buffer->uv[1] = v;
        }
    }

    void TexCoord3(float u, float v, float s, int channel = 0) {}
    void TexCoord4(float u, float v, float s, float t, int channel = 0) {}

    void Specular(pddiColour colour) 
    {
        //
    }

    void SkinIndices(unsigned, unsigned, unsigned, unsigned)
    {
    }

    void SkinWeights(float, float, float)
    {
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    bool CheckMemImageVersion(int version) { return false; }
    void* GetMemImagePtr()                 { return NULL; }
    unsigned GetMemImageLength()           { return 0; }

};

pglPrimBuffer::pglPrimBuffer(pglContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex) : context(c)
{
    stream = new pglPrimBufferStream(this);

    total = allocated = nStrips = 0;
    coord = normal = uv = NULL;
    colour = NULL;
    strips = NULL;
    indices = NULL;

    primType = type;
    vertexType = vertexFormat;

    allocated = nVertex;
    
    coord = new float[3 * nVertex];
    mem = 12;

    if(vertexFormat & PDDI_V_NORMAL) 
    {
        normal = new float[3 * nVertex];
        mem += 12;
    }

    if(vertexFormat & 0xf) 
    {
        uv = new float[2 * nVertex];
        mem += 8;
    }

    if(vertexFormat & PDDI_V_COLOUR) 
    {
        colour = new unsigned char[4 * nVertex];
        mem += 4;
    }

    mem *= nVertex;

    indexCount = nIndex;
    if(indexCount) 
    {
        indices = new unsigned short[indexCount];
        memset(indices, 0, indexCount * sizeof(unsigned short));   // Linux port: never draw garbage indices
    }
    indexUsed = indexCount;
    useSize = 0;
    coordBase = coord;
    normalBase = normal;
    uvBase = uv;
    colourBase = colour;

    mem += nIndex * 2;

    nStrips = 0;
    strips = NULL;

    vertexBuffer = indexBuffer = 0;
    verticesChanged = indicesChanged = true;
    uploads = 0;

    mem /= 1024.0f;

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, 1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, mem);
}

pglPrimBuffer::~pglPrimBuffer()
{
    delete stream;

    delete [] strips;
    delete [] indices;
    delete [] coordBase;
    delete [] normalBase;
    delete [] uvBase;
    delete [] colourBase;

    pglDeleteBuffer(vertexBuffer);
    pglDeleteBuffer(indexBuffer);

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, -1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, -mem);
}

pddiPrimBufferStream* pglPrimBuffer::Lock()
{
    total = 0;
    return stream;
}

void pglPrimBuffer::Unlock(pddiPrimBufferStream* stream)
{
    // Linux port: back to the starts (the stream stops advancing at the allocation, see Next)
    coord = coordBase;
    normal = normalBase;
    uv = uvBase;
    colour = colourBase;
    if(total > allocated)
    {
        static int reported = 0;
        if(reported++ < 8)
            fprintf(stderr, "pddigl: prim buffer filled with %u vertices, has room for %u; the rest dropped\n", total, allocated);
        total = allocated;
    }
    verticesChanged = true;     // uploaded by Display, on the render thread (this may be a loader thread)
}

unsigned char* pglPrimBuffer::LockIndexBuffer()
{
    PDDIASSERT(0);
    return NULL;
}

void pglPrimBuffer::UnlockIndexBuffer(int count)
{
    PDDIASSERT(0);
}

void pglPrimBuffer::SetIndices(unsigned short* i, int count)
{
    PDDIASSERT(count <= (int)indexCount);
    // Linux port: as pddidx8, the count set is the count drawn; out-of-range indices (D3D read past the vertex
    // buffer harmlessly, GL's client arrays read any memory) become 0
    if(count < 0)
        count = 0;
    if(count > (int)indexCount)
        count = indexCount;
    memcpy(indices, i, count * sizeof(unsigned short));
    indexUsed = count;
    indicesChanged = true;
    int bad = 0;
    for(int n = 0; n < count; n++)
    {
        if(indices[n] >= allocated)
        {
            indices[n] = 0;
            bad++;
        }
    }
    if(bad)
    {
        static int reported = 0;
        if(reported++ < 8)
            fprintf(stderr, "pddigl: %d of %d indices past the %u vertices of a prim buffer, drawn as vertex 0\n", bad, count, allocated);
    }
}

// Linux port: the arrays live in GPU buffers (one for the vertex attributes, one for the indices), uploaded here
// when they changed. Attributes the format lacks take the current values, as fixed-function OpenGL did.
void pglPrimBuffer::Upload(void)
{
    if(!vertexBuffer)
        glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    if(verticesChanged)
    {
        // a buffer refilled after its first upload (particles, software skinning) is probably refilled every frame
        GLenum usage = uploads++ ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        unsigned n = allocated;
        normalOffset = n * 12;
        uvOffset = normalOffset + (normal ? n * 12 : 0);
        colourOffset = uvOffset + (uv ? n * 8 : 0);
        unsigned size = colourOffset + (colour ? n * 4 : 0);
        glBufferData(GL_ARRAY_BUFFER, size, NULL, usage);
        context->GetDisplay()->GetPipeline()->stats.bufferUploads++;
        context->GetDisplay()->GetPipeline()->stats.bufferBytes += size;
#ifdef __APPLE__
        // the storage was just orphaned; glBufferSubData would still make Apple's GL submit and wait (glpipeline.cpp)
        char* dst = (char*)glMapBufferRange(GL_ARRAY_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if(dst)
        {
            memcpy(dst, coordBase, n * 12);
            if(normal)
                memcpy(dst + normalOffset, normalBase, n * 12);
            if(uv)
                memcpy(dst + uvOffset, uvBase, n * 8);
            if(colour)
                memcpy(dst + colourOffset, colourBase, n * 4);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        else
#endif
        {
        glBufferSubData(GL_ARRAY_BUFFER, 0, n * 12, coordBase);
        if(normal)
            glBufferSubData(GL_ARRAY_BUFFER, normalOffset, n * 12, normalBase);
        if(uv)
            glBufferSubData(GL_ARRAY_BUFFER, uvOffset, n * 8, uvBase);
        if(colour)
            glBufferSubData(GL_ARRAY_BUFFER, colourOffset, n * 4, colourBase);
        }
        verticesChanged = false;
    }

    if(indexCount && indices)
    {
        if(!indexBuffer)
            glGenBuffers(1, &indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        if(indicesChanged)
        {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(unsigned short), indices, GL_STATIC_DRAW);
            indicesChanged = false;
        }
    }
}

void pglPrimBuffer::Display(void)
{
    pglPipeline* pipeline = context->GetDisplay()->GetPipeline();
    pipeline->Prepare();
    Upload();

    glVertexAttribPointer(PGL_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);

    if(vertexType & PDDI_V_NORMAL)
    {
        glEnableVertexAttribArray(PGL_ATTRIB_NORMAL);
        glVertexAttribPointer(PGL_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, 0, (void*)(size_t)normalOffset);
    }
    else
    {
        pipeline->UseCurrent(PGL_ATTRIB_NORMAL);
    }

    if(vertexType & PDDI_V_COLOUR)
    {
        glEnableVertexAttribArray(PGL_ATTRIB_COLOUR);
        glVertexAttribPointer(PGL_ATTRIB_COLOUR, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (void*)(size_t)colourOffset);
    }
    else
    {
        pipeline->UseCurrent(PGL_ATTRIB_COLOUR);
    }

    if(vertexType & 0xf)
    {
        glEnableVertexAttribArray(PGL_ATTRIB_UV);
        glVertexAttribPointer(PGL_ATTRIB_UV, 2, GL_FLOAT, GL_FALSE, 0, (void*)(size_t)uvOffset);
    }
    else
    {
        pipeline->UseCurrent(PGL_ATTRIB_UV);
    }

    if(indexCount && indices)
    {
        unsigned count = (useSize && useSize < indexUsed) ? useSize : indexUsed;
        glDrawElements(primTypeTable[primType],count,GL_UNSIGNED_SHORT,(void*)0);
    }
    else
    {
        unsigned count = (useSize && useSize < total) ? useSize : total;
        glDrawArrays(primTypeTable[primType], 0, count);
    }
}

/*
protected:
    float* coord;
    float* normal;
    float* uv;
    unsigned char* colour;

    unsigned allocated;
    unsigned total;

};
*/

void pglContext::DrawPrimBuffer(pddiShader* mat, pddiPrimBuffer* buffer)
{
    if(!mat)
        mat = defaultShader;

    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    // as the D3D8 pddi, once per pass (Linux port: user material shaders can have several, glcustom.cpp)
    int passes = material->GetPasses();
    for(int i = 0; i < passes; i++)
    {
        material->SetMaterial(i);
        ((pglPrimBuffer*)buffer)->Display();
    }
}

// lighting

int pglContext::GetMaxLights(void)
{
    return pglMaxLights;    // Linux port: GL_MAX_LIGHTS was 8; the pipeline's shader has as many
}

void pglContext::SetupHardwareLight(int handle)
{
    pddiLight& light = state.lightingState->light[handle];

    float c[4];
    FillGLColour(light.colour, c);
    
    float dir[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
    switch(light.type)
    {
        case PDDI_LIGHT_DIRECTIONAL :
            dir[0] = -light.worldDirection.x;
            dir[1] = -light.worldDirection.y;
            dir[2] = -light.worldDirection.z;
            dir[3] = 0.0f;
            break;

        case PDDI_LIGHT_POINT :
            dir[0] = light.worldPosition.x;
            dir[1] = light.worldPosition.y;
            dir[2] = light.worldPosition.z;
            dir[3] = 1.0f;
            break;

        case PDDI_LIGHT_SPOT :
            PDDIASSERT(0);
            break;
    }
    pipeline->SetLight(handle, light.enabled, dir, c, light.attenA, light.attenB, light.attenC, light.range);
}

void pglContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
    float ambient[4];
    FillGLColour(col,ambient);
    pipeline->SetAmbient(ambient);
}


// backface culling
GLenum cullModeTable[3] =
{
    GL_FRONT, // PDDI_CULL_NONE (disabled using glDisable())
    GL_FRONT, // PDDI_CULL_NORMAL
    GL_BACK   // PDDI_CULL_INVERTED
};
    
void pglContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);
    ApplyCull();
}

void pglContext::ApplyCull(void)
{
    pddiCullMode mode = materialCull == PGL_CULL_NONE ? PDDI_CULL_NONE : state.renderState->cullMode;

    if(mode == PDDI_CULL_NONE)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(cullModeTable[mode]);
    }
}

void pglContext::SetMaterialCull(int cull)
{
    if(cull == materialCull)
        return;
    materialCull = cull;
    ApplyCull();
}

// z-buffer control
GLenum compTable[8] = {
    GL_NEVER,
    GL_ALWAYS,  
    GL_LESS,
    GL_LEQUAL,
    GL_GREATER,    
    GL_GEQUAL,  
    GL_EQUAL,
    GL_NOTEQUAL,
};

void pglContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
    if(enable)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
}


void pglContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    glDepthFunc(compTable[compareMode]);
}

void pglContext::SetZWrite(bool b)
{
    pddiBaseContext::SetZWrite(b);
    glDepthMask(b);
}

// Linux port: was missing, so masks drawn with colour writes off (e.g. the HUD map's round "hole") appeared
void pglContext::SetColourWrite(bool red, bool green, bool blue, bool alpha)
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
    glColorMask(red, green, blue, alpha);
}

void pglContext::SetZBias(float bias)
{
    pddiBaseContext::SetZBias(bias);
//TODO : Figure out how ro do this
}

void pglContext::SetZRange(float n, float f)
{
    pddiBaseContext::SetZRange(n,f);
    glDepthRange(n,f);
}

// stencil buffer control
GLenum stencilTable[6] = {
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_DECR,
    GL_INVERT
};

void pglContext::EnableStencilBuffer(bool enable)
{
    pddiBaseContext::EnableStencilBuffer(enable);
    if(enable)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
}
        
void pglContext::SetStencilCompare(pddiCompareMode compare)
{
    pddiBaseContext::SetStencilCompare(compare);
    glStencilFunc(compTable[compare], state.stencilState->ref, state.stencilState->mask);
}

void pglContext::SetStencilRef(int ref)
{
    pddiBaseContext::SetStencilRef(ref);
    glStencilFunc(compTable[state.stencilState->compare], ref, state.stencilState->mask);
}

void pglContext::SetStencilMask(unsigned mask)
{
    pddiBaseContext::SetStencilMask(mask);
    glStencilFunc(compTable[state.stencilState->compare], state.stencilState->ref, mask);
}

void pglContext::SetStencilWriteMask(unsigned mask)
{
    pddiBaseContext::SetStencilWriteMask(mask);
    glStencilMask(mask);
}

void pglContext::SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp)
{
    pddiBaseContext::SetStencilOp(failOp, zFailOp, zPassOp);
    glStencilOp(stencilTable[failOp],stencilTable[zFailOp],stencilTable[zPassOp]);
}

// polygon fill
GLenum fillTable[3] =
{
    GL_FILL,
    GL_LINE,
    GL_POINT
};

void pglContext::SetFillMode(pddiFillMode mode)
{
    pddiBaseContext::SetFillMode(mode);
    glPolygonMode(GL_FRONT_AND_BACK, fillTable[mode]);
}

// fog
void pglContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);
    pipeline->EnableFog(enable);
}

void pglContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour,start,end);

    float fog[4];
    FillGLColour(colour, fog);
    pipeline->SetFog(fog, start, end);     // linear, the only mode the pddi used
}


int pglContext::GetMaxTextureDimension(void)
{
#ifdef RAD_LINUX
    extern int g_pglMaxTextureSize;      // cached on the render thread (may be called from loader threads)
    // never query GL here: on a loader thread there is no current context, and glvnd's dispatch crashed
    // (tSprite::SetBlitBorder) when the cache was empty, as when display init ran out of video memory
    return g_pglMaxTextureSize > 0 ? g_pglMaxTextureSize : 2048;
#endif
    int size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&size);
    return size;
}

pddiExtension* pglContext::GetExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GL_CONTEXT :
            return extContext;
        case PDDI_EXT_GAMMACONTROL :
            return extGamma;
    }

    return pddiBaseContext::GetExtension(extID);
}

bool pglContext::VerifyExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GL_CONTEXT :
        case PDDI_EXT_GAMMACONTROL :
            return true;
    }

    return pddiBaseContext::VerifyExtension(extID);
}

void  pglContext::BeginTiming(void)
{
    display->BeginTiming();
}

float pglContext::EndTiming(void)
{
    return display->EndTiming();
}



