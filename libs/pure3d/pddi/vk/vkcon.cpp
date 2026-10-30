//=============================================================================
// Linux port: Vulkan renderer context and prim buffers (see vkcon.hpp). The pddi logic is glcon.cpp's: keep the two
// in step (D3D8 parity fixes apply to both).
//=============================================================================

#include <pddi/vk/vkcon.hpp>
#include <pddi/vk/vkdev.hpp>
#include <pddi/vk/vkdisplay.hpp>
#include <pddi/vk/vkmat.hpp>
#include <pddi/vk/vktex.hpp>

#include <pddi/base/debug.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>

static inline void FillColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}

class pvkExtGamma : public pddiExtGammaControl
{
public:
    pvkExtGamma(pvkDisplay* d) { display = d; }
    void SetGamma(float r, float g, float b)     { display->SetGamma(r, g, b); }
    void GetGamma(float* r, float* g, float* b)  { display->GetGamma(r, g, b); }
protected:
    pvkDisplay* display;
};

pvkContext::pvkContext(pvkDevice* dev, pvkDisplay* disp) : pddiBaseContext((pddiDisplay*)disp, (pddiDevice*)dev)
{
    device = dev;
    display = disp;
    pipeline = disp->GetPipeline();
    materialCull = PVK_CULL_GAME;
    device->AddRef();
    display->AddRef();
    disp->SetContext(this);
    DefaultState();
    extGamma = new pvkExtGamma(display);
    defaultShader = new pvkMat(this);
    defaultShader->AddRef();
    SyncState(0xffffffff);
}

pvkContext::~pvkContext()
{
    defaultShader->Release();
    delete extGamma;
    display->SetContext(NULL);
    display->Release();
    device->Release();
}

void pvkContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();
    pipeline->current.colour[0] = pipeline->current.colour[1] = pipeline->current.colour[2] = pipeline->current.colour[3] = 255;
    pipeline->NewFrame(display->GetWidth(), display->GetHeight());
    pvkCustomUpdate(pipeline);
    pipeline->LoadIdentity();
    pipeline->FrameStats();
    display->BeginFrameTarget();
}

void pvkContext::EndFrame()
{
    pddiBaseContext::EndFrame();
    pipeline->FrameEnd();
}

// as Direct3D's Clear (glcon.cpp): write masks do not apply; the scissor rectangle does, as with glClear
void pvkContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);
    if(!g_pvk)
        return;
    g_pvk->BeginRendering();
    VkClearAttachment attachments[2];
    unsigned n = 0;
    if(bufferMask & PDDI_BUFFER_COLOUR)
    {
        attachments[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[n].colorAttachment = 0;
        attachments[n].clearValue.color.float32[0] = float(state.viewState->clearColour.Red()) / 255.0f;
        attachments[n].clearValue.color.float32[1] = float(state.viewState->clearColour.Green()) / 255.0f;
        attachments[n].clearValue.color.float32[2] = float(state.viewState->clearColour.Blue()) / 255.0f;
        attachments[n].clearValue.color.float32[3] = float(state.viewState->clearColour.Alpha()) / 255.0f;
        n++;
    }
    VkImageAspectFlags ds = ((bufferMask & PDDI_BUFFER_DEPTH) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                            ((bufferMask & PDDI_BUFFER_STENCIL) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    if(ds)
    {
        attachments[n].aspectMask = ds;
        attachments[n].colorAttachment = 0;
        attachments[n].clearValue.depthStencil.depth = state.viewState->clearDepth;
        attachments[n].clearValue.depthStencil.stencil = state.viewState->clearStencil;
        n++;
    }
    if(!n)
        return;
    int w = g_pvk->Width(), h = g_pvk->Height();
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    const pvkPipeline::RenderState& s = pipeline->state;
    if(s.scissor)
    {
        x0 = s.scissorRect[0] < 0 ? 0 : s.scissorRect[0];
        y0 = s.scissorRect[1] < 0 ? 0 : s.scissorRect[1];
        x1 = s.scissorRect[0] + s.scissorRect[2] > w ? w : s.scissorRect[0] + s.scissorRect[2];
        y1 = s.scissorRect[1] + s.scissorRect[3] > h ? h : s.scissorRect[1] + s.scissorRect[3];
        if(x1 <= x0 || y1 <= y0)
            return;
    }
    VkClearRect rect = { { { x0, y0 }, { (unsigned)(x1 - x0), (unsigned)(y1 - y0) } }, 0, 1 };
    vkCmdClearAttachments(g_pvk->Cmd(), n, attachments, 1, &rect);
}

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

// viewports in pixels from the top (glcon.cpp's from the bottom; the pipeline flips y as GL)
void pvkContext::SetupHardwareProjection(void)
{
    float m[16];
    int w = display->GetWidth(), h = display->GetHeight();
    int* v = pipeline->state.viewport;
    switch(state.viewState->projectionMode)
    {
        case PDDI_PROJECTION_DEVICE :
            Ortho(m, 0, w, h, 0, state.viewState->camera.nearPlane, state.viewState->camera.farPlane);
            pipeline->SetProjection(m);
            v[0] = 0;
            v[1] = 0;
            v[2] = w;
            v[3] = h;
            break;

        case PDDI_PROJECTION_ORTHOGRAPHIC :
        case PDDI_PROJECTION_PERSPECTIVE :
            if(state.viewState->projectionMode == PDDI_PROJECTION_ORTHOGRAPHIC)
            {
                Ortho(m, -0.5, 0.5, -((1 / state.viewState->camera.aspect) / 2), ((1 / state.viewState->camera.aspect) / 2),
                      state.viewState->camera.nearPlane, state.viewState->camera.farPlane);
            }
            else
            {
                float halfX = (float)tan(double(state.viewState->camera.fov * 0.5)) * state.viewState->camera.nearPlane;
                float halfY = halfX / state.viewState->camera.aspect;
                Frustum(m, -halfX, halfX, -halfY, halfY, state.viewState->camera.nearPlane, state.viewState->camera.farPlane);
            }
            pipeline->SetProjection(m);
            v[0] = int(state.viewState->viewWindow.left * w);
            v[1] = int(state.viewState->viewWindow.top * h);
            v[2] = int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * w);
            v[3] = int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * h);
            break;

        default:
            PDDIASSERTMSG(0, "Bad projection mode", "");
            break;
    }
}

void pvkContext::LoadHardwareMatrix(pddiMatrixType id)
{
    if(id != PDDI_MATRIX_MODELVIEW)
    {
        PDDIASSERTMSG(0, "Invalid matrix load", "");
        return;
    }
    pddiMatrix tmp = *state.matrixStack[id]->Top();
    tmp.m[0][2] = -tmp.m[0][2];
    tmp.m[1][2] = -tmp.m[1][2];
    tmp.m[2][2] = -tmp.m[2][2];
    tmp.m[3][2] = -tmp.m[3][2];
    pipeline->SetModelView((float*)&tmp);
}

void pvkContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);
    pipeline->state.scissor = rect != NULL;
    if(rect)
    {
        pipeline->state.scissorRect[0] = rect->left;
        pipeline->state.scissorRect[1] = rect->top;
        pipeline->state.scissorRect[2] = rect->right - rect->left;
        pipeline->state.scissorRect[3] = rect->bottom - rect->top;
    }
}

// immediate mode, as glcon.cpp's pglPrimStream
class pvkPrimStream : public pddiPrimStream
{
public:
    pvkPipeline* pipe;

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
    void Coord(float x, float y, float z)                   { pipe->Emit(x, y, z); }
    void Normal(float x, float y, float z)                  { SetNormal(x, y, z); }
    void Colour(pddiColour colour, int channel = 0)         { SetColour(colour); }
    void UV(float u, float v, int channel = 0)              { if(channel == 0) SetUV(u, v); }
    void Specular(pddiColour colour)                        { }
    void Vertex(pddiVector* v, pddiColour c)                { SetColour(c); pipe->Emit(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiVector* n)               { SetNormal(n->x, n->y, n->z); pipe->Emit(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiVector2* uv)             { SetUV(uv->u, uv->v); pipe->Emit(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        SetColour(c);
        SetUV(uv->u, uv->v);
        pipe->Emit(v->x, v->y, v->z);
    }
    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        SetNormal(n->x, n->y, n->z);
        SetUV(uv->u, uv->v);
        pipe->Emit(v->x, v->y, v->z);
    }
};
static pvkPrimStream thePrimStream;

pddiPrimStream* pvkContext::BeginPrims(pddiShader* mat, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    if(!mat)
        mat = defaultShader;
    pddiBaseContext::BeginPrims(mat, primType, vertexType, vertexCount);
    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    material->SetMaterial();
    thePrimStream.pipe = pipeline;
    pipeline->BeginStream(primType);
    return &thePrimStream;
}

void pvkContext::EndPrims(pddiPrimStream* stream)
{
    pddiBaseContext::EndPrims(stream);
    pipeline->EndStream();
}

//-----------------------------------------------------------------------------
// Prim buffers
//-----------------------------------------------------------------------------

class pvkPrimBufferStream : public pddiPrimBufferStream
{
public:
    pvkPrimBuffer* buffer;

    pvkPrimBufferStream(pvkPrimBuffer* b) { buffer = b; }

    void Next(void)
    {
        if(buffer->coord)  buffer->coord += 3;
        if(buffer->normal) buffer->normal += 3;
        if(buffer->uv)     buffer->uv += 2;
        if(buffer->colour) buffer->colour += 4;
        buffer->total++;
        PDDIASSERT(buffer->total <= buffer->allocated);
        if(buffer->total >= buffer->allocated)
        {
            // past the allocation, further vertices overwrite the last one (as glcon.cpp)
            unsigned last = buffer->allocated > 0 ? buffer->allocated - 1 : 0;
            if(buffer->coord)  buffer->coord = buffer->coordBase + last * 3;
            if(buffer->normal) buffer->normal = buffer->normalBase + last * 3;
            if(buffer->uv)     buffer->uv = buffer->uvBase + last * 2;
            if(buffer->colour) buffer->colour = buffer->colourBase + last * 4;
        }
    }
    void SetColour(pddiColour c)
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
    }
    void SetCoord(pddiVector* v)
    {
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
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
    void Colour(pddiColour colour, int channel = 0)         { SetColour(colour); }
    void TexCoord1(float u, int channel = 0)                { }
    void TexCoord2(float u, float v, int channel = 0)
    {
        if(channel == 0)
        {
            buffer->uv[0] = u;
            buffer->uv[1] = v;
        }
    }
    void TexCoord3(float u, float v, float s, int channel = 0) { }
    void TexCoord4(float u, float v, float s, float t, int channel = 0) { }
    void Specular(pddiColour colour)                        { }
    void SkinIndices(unsigned, unsigned, unsigned, unsigned) { }
    void SkinWeights(float, float, float)                   { }
    void Vertex(pddiVector* v, pddiColour c)                { SetColour(c); SetCoord(v); Next(); }
    void Vertex(pddiVector* v, pddiVector* n)               { Normal(n->x, n->y, n->z); SetCoord(v); Next(); }
    void Vertex(pddiVector* v, pddiVector2* uv)             { TexCoord2(uv->u, uv->v); SetCoord(v); Next(); }
    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        SetColour(c);
        TexCoord2(uv->u, uv->v);
        SetCoord(v);
        Next();
    }
    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        Normal(n->x, n->y, n->z);
        TexCoord2(uv->u, uv->v);
        SetCoord(v);
        Next();
    }
    bool CheckMemImageVersion(int version) { return false; }
    void* GetMemImagePtr()                 { return NULL; }
    unsigned GetMemImageLength()           { return 0; }
};

pvkPrimBuffer::pvkPrimBuffer(pvkContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex) : context(c)
{
    stream = new pvkPrimBufferStream(this);
    total = 0;
    coord = normal = uv = NULL;
    colour = NULL;
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
        memset(indices, 0, indexCount * sizeof(unsigned short));
    }
    indexUsed = indexCount;
    useSize = 0;
    coordBase = coord;
    normalBase = normal;
    uvBase = uv;
    colourBase = colour;
    mem += nIndex * 2;
    memset(&buffer, 0, sizeof(buffer));
    normalOffset = uvOffset = colourOffset = indexOffset = 0;
    changed = true;
    mem /= 1024.0f;
    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, 1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, mem);
}

pvkPrimBuffer::~pvkPrimBuffer()
{
    delete stream;
    delete [] indices;
    delete [] coordBase;
    delete [] normalBase;
    delete [] uvBase;
    delete [] colourBase;
    if(g_pvk)
        g_pvk->Retire(buffer);
    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, -1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, -mem);
}

pddiPrimBufferStream* pvkPrimBuffer::Lock()
{
    total = 0;
    return stream;
}

void pvkPrimBuffer::Unlock(pddiPrimBufferStream* stream)
{
    coord = coordBase;
    normal = normalBase;
    uv = uvBase;
    colour = colourBase;
    if(total > allocated)
    {
        static int reported = 0;
        if(reported++ < 8)
            fprintf(stderr, "pddivk: prim buffer filled with %u vertices, has room for %u; the rest dropped\n", total, allocated);
        total = allocated;
    }
    changed = true;
}

unsigned char* pvkPrimBuffer::LockIndexBuffer()
{
    PDDIASSERT(0);
    return NULL;
}

void pvkPrimBuffer::UnlockIndexBuffer(int count)
{
    PDDIASSERT(0);
}

void pvkPrimBuffer::SetIndices(unsigned short* i, int count)
{
    PDDIASSERT(count <= (int)indexCount);
    if(count < 0)
        count = 0;
    if(count > (int)indexCount)
        count = indexCount;
    memcpy(indices, i, count * sizeof(unsigned short));
    indexUsed = count;
    changed = true;
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
            fprintf(stderr, "pddivk: %d of %d indices past the %u vertices of a prim buffer, drawn as vertex 0\n", bad, count, allocated);
    }
}

// a new device buffer each time the arrays changed (the old one is retired: draws earlier in this frame, or in
// frames in flight, keep reading it), filled from the frame's ring by the upload command buffer
bool pvkPrimBuffer::Upload(void)
{
    if(!changed && buffer.buffer)
        return true;
    changed = false;
    g_pvk->Retire(buffer);
    VkDeviceSize n = allocated;
    normalOffset = n * 12;
    uvOffset = normalOffset + (normal ? n * 12 : 0);
    colourOffset = uvOffset + (uv ? n * 8 : 0);
    indexOffset = (colourOffset + (colour ? n * 4 : 0) + 3) & ~(VkDeviceSize)3;
    VkDeviceSize size = indexOffset + (indices ? indexCount * 2 : 0);
    if(size == 0)
        return false;
    if(!g_pvk->CreateBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, &buffer))
    {
        changed = true;
        return false;
    }
    VkBuffer staging;
    VkDeviceSize offset;
    unsigned char* dst = (unsigned char*)g_pvk->Transient(size, 16, &staging, &offset);
    if(!dst)
    {
        g_pvk->Retire(buffer);
        changed = true;
        return false;
    }
    memcpy(dst, coordBase, n * 12);
    if(normal)
        memcpy(dst + normalOffset, normalBase, n * 12);
    if(uv)
        memcpy(dst + uvOffset, uvBase, n * 8);
    if(colour)
        memcpy(dst + colourOffset, colourBase, n * 4);
    if(indices)
        memcpy(dst + indexOffset, indices, indexCount * 2);
    VkBufferCopy copy = { offset, 0, size };
    vkCmdCopyBuffer(g_pvk->UploadCmd(), staging, buffer.buffer, 1, &copy);
    pvkPipeline* pipeline = context->GetPipeline();
    pipeline->stats.uploads++;
    return true;
}

static const int s_primTypes[5] = { PVK_TRIANGLES, PVK_TRISTRIP, PVK_LINES, PVK_LINESTRIP, PVK_POINTS };

void pvkPrimBuffer::Display(void)
{
    if(!g_pvk || !Upload())
        return;
    unsigned count = indices ? ((useSize && useSize < indexUsed) ? useSize : indexUsed)
                             : ((useSize && useSize < total) ? useSize : total);
    if(count == 0)
        return;
    unsigned missing = (normal ? 0 : 1u << PVK_ATTRIB_NORMAL) | (colour ? 0 : 1u << PVK_ATTRIB_COLOUR) |
                       (uv ? 0 : 1u << PVK_ATTRIB_UV);
    pvkPipeline* pipeline = context->GetPipeline();
    VkCommandBuffer cmd = pipeline->Prepare(s_primTypes[primType], missing);
    if(!cmd)
        return;
    VkBuffer buffers[PVK_ATTRIB_COUNT] = { buffer.buffer, buffer.buffer, buffer.buffer, buffer.buffer };
    VkDeviceSize offsets[PVK_ATTRIB_COUNT] = { 0, normalOffset, colourOffset, uvOffset };
    VkDeviceSize strides[PVK_ATTRIB_COUNT] = { 12, 12, 4, 8 };
    pipeline->BindCurrent(cmd, missing, buffers, offsets, strides);
    vkCmdBindVertexBuffers2(cmd, 0, PVK_ATTRIB_COUNT, buffers, offsets, NULL, strides);
    if(indices)
    {
        vkCmdBindIndexBuffer(cmd, buffer.buffer, indexOffset, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
    }
    else
        vkCmdDraw(cmd, count, 1, 0, 0);
}

void pvkContext::DrawPrimBuffer(pddiShader* mat, pddiPrimBuffer* buffer)
{
    if(!mat)
        mat = defaultShader;
    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    int passes = material->GetPasses();
    for(int i = 0; i < passes; i++)
    {
        material->SetMaterial(i);
        ((pvkPrimBuffer*)buffer)->Display();
    }
}

//-----------------------------------------------------------------------------
// Lighting and state (glcon.cpp)
//-----------------------------------------------------------------------------

int pvkContext::GetMaxLights(void)
{
    return pvkMaxLights;
}

void pvkContext::SetupHardwareLight(int handle)
{
    pddiLight& light = state.lightingState->light[handle];
    float c[4];
    FillColour(light.colour, c);
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

void pvkContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
    float ambient[4];
    FillColour(col, ambient);
    pipeline->SetAmbient(ambient);
}

void pvkContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);
    ApplyCull();
}

// as glcon.cpp: PDDI_CULL_NORMAL culls GL's front faces, INVERTED the back ones
void pvkContext::ApplyCull(void)
{
    pddiCullMode mode = materialCull == PVK_CULL_NONE ? PDDI_CULL_NONE : state.renderState->cullMode;
    pipeline->state.cull = mode != PDDI_CULL_NONE;
    pipeline->state.cullFront = mode != PDDI_CULL_INVERTED;
}

void pvkContext::SetMaterialCull(int cull)
{
    if(cull == materialCull)
        return;
    materialCull = cull;
    ApplyCull();
}

void pvkContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
    pipeline->state.depthTest = enable;
}

void pvkContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    pipeline->state.depthCompare = compareMode;
}

void pvkContext::SetZWrite(bool b)
{
    pddiBaseContext::SetZWrite(b);
    pipeline->state.depthWrite = b;
}

void pvkContext::SetColourWrite(bool red, bool green, bool blue, bool alpha)
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
    pipeline->state.colourWrite = (red ? 1 : 0) | (green ? 2 : 0) | (blue ? 4 : 0) | (alpha ? 8 : 0);
}

void pvkContext::SetZBias(float bias)
{
    pddiBaseContext::SetZBias(bias);
}

void pvkContext::SetZRange(float n, float f)
{
    pddiBaseContext::SetZRange(n, f);
    pipeline->state.depthMin = n;
    pipeline->state.depthMax = f;
}

void pvkContext::EnableStencilBuffer(bool enable)
{
    pddiBaseContext::EnableStencilBuffer(enable);
    pipeline->state.stencilTest = enable;
}

void pvkContext::SetStencilCompare(pddiCompareMode compare)
{
    pddiBaseContext::SetStencilCompare(compare);
    pipeline->state.stencilCompare = compare;
}

void pvkContext::SetStencilRef(int ref)
{
    pddiBaseContext::SetStencilRef(ref);
    pipeline->state.stencilRef = ref;
}

void pvkContext::SetStencilMask(unsigned mask)
{
    pddiBaseContext::SetStencilMask(mask);
    pipeline->state.stencilMask = mask;
}

void pvkContext::SetStencilWriteMask(unsigned mask)
{
    pddiBaseContext::SetStencilWriteMask(mask);
    pipeline->state.stencilWriteMask = mask;
}

void pvkContext::SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp)
{
    pddiBaseContext::SetStencilOp(failOp, zFailOp, zPassOp);
    pipeline->state.stencilFail = failOp;
    pipeline->state.stencilZFail = zFailOp;
    pipeline->state.stencilZPass = zPassOp;
}

void pvkContext::SetFillMode(pddiFillMode mode)
{
    pddiBaseContext::SetFillMode(mode);
    pipeline->state.fill = mode;
}

void pvkContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);
    pipeline->EnableFog(enable);
}

void pvkContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour, start, end);
    float fog[4];
    FillColour(colour, fog);
    pipeline->SetFog(fog, start, end);
}

int pvkContext::GetMaxTextureDimension(void)
{
    return g_pvk ? (int)g_pvk->properties.limits.maxImageDimension2D : 2048;
}

pddiExtension* pvkContext::GetExtension(unsigned extID)
{
    if(extID == PDDI_EXT_GAMMACONTROL)
        return extGamma;
    return pddiBaseContext::GetExtension(extID);
}

bool pvkContext::VerifyExtension(unsigned extID)
{
    if(extID == PDDI_EXT_GAMMACONTROL)
        return true;
    return pddiBaseContext::VerifyExtension(extID);
}

void pvkContext::BeginTiming(void)
{
    display->BeginTiming();
}

float pvkContext::EndTiming(void)
{
    return display->EndTiming();
}
