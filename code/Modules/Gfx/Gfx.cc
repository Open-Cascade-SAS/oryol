//------------------------------------------------------------------------------
//  Gfx.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "Gfx.h"
#include "Core/Core.h"
#include "Core/Trace.h"
#include "Gfx/private/gfxPointers.h"
#include "Gfx/private/gfxBackend.h"

namespace Oryol {

using namespace _priv;

namespace {
    struct _state {
        class GfxSetup gfxSetup;
        GfxFrameInfo gfxFrameInfo;
        RunLoop::Id runLoopId = RunLoop::InvalidId;
        _priv::gfxBackend backend;
        bool inPass = false;
    };
    _state* state = nullptr;
}

//------------------------------------------------------------------------------
void
Gfx::Setup(const class GfxSetup& setup) {
    o_assert_dbg(!IsValid());
    state = Memory::New<_state>();
    state->gfxSetup = setup;

    gfxPointers pointers;
    pointers.displayMgr = &state->backend.displayManager;
    pointers.renderer = &state->backend.renderer;
    pointers.resContainer = &state->backend.resourceContainer;
    pointers.meshPool = &state->backend.resourceContainer.meshPool;
    pointers.shaderPool = &state->backend.resourceContainer.shaderPool;
    pointers.texturePool = &state->backend.resourceContainer.texturePool;
    pointers.pipelinePool = &state->backend.resourceContainer.pipelinePool;
    pointers.renderPassPool = &state->backend.resourceContainer.renderPassPool;
    
    state->backend.Setup(setup, pointers);
    state->runLoopId = Core::PreRunLoop()->Add([] {
        state->backend.ProcessSystemEvents();
    });
    state->gfxFrameInfo = GfxFrameInfo();
}

//------------------------------------------------------------------------------
void
Gfx::Discard() {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    Core::PreRunLoop()->Remove(state->runLoopId);
    state->backend.Discard();
    Memory::Delete(state);
    state = nullptr;
}

//------------------------------------------------------------------------------
bool
Gfx::IsValid() {
    return nullptr != state;
}

//------------------------------------------------------------------------------
bool
Gfx::QuitRequested() {
    o_assert_dbg(IsValid());
    return state->backend.QuitRequested();
}

//------------------------------------------------------------------------------
GfxEvent::HandlerId
Gfx::Subscribe(GfxEvent::Handler handler) {
    o_assert_dbg(IsValid());
    return state->backend.Subscribe(handler);
}

//------------------------------------------------------------------------------
void
Gfx::Unsubscribe(GfxEvent::HandlerId id) {
    o_assert_dbg(IsValid());
    state->backend.Unsubscribe(id);
}

//------------------------------------------------------------------------------
const GfxSetup&
Gfx::GfxSetup() {
    o_assert_dbg(IsValid());
    return state->gfxSetup;
}

//------------------------------------------------------------------------------
const DisplayAttrs&
Gfx::DisplayAttrs() {
    o_assert_dbg(IsValid());
    return state->backend.displayManager.GetDisplayAttrs();
}

//------------------------------------------------------------------------------
const DisplayAttrs&
Gfx::PassAttrs() {
    o_assert_dbg(IsValid());
    return state->backend.renderer.renderPassAttrs();
}

//------------------------------------------------------------------------------
const GfxFrameInfo&
Gfx::FrameInfo() {
    o_assert_dbg(IsValid());
    return state->gfxFrameInfo;
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass() {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    state->backend.renderer.beginPass(nullptr, &state->gfxSetup.DefaultPassAction);
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass(const PassAction& action) {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    state->backend.renderer.beginPass(nullptr, &action);
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass(const Id& id) {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    renderPass* pass = state->backend.resourceContainer.lookupRenderPass(id);
    o_assert_dbg(pass);
    state->backend.renderer.beginPass(pass, &pass->Setup.DefaultAction);
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass(const Id& id, const PassAction& passAction) {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    renderPass* pass = state->backend.resourceContainer.lookupRenderPass(id);
    o_assert_dbg(pass);
    state->backend.renderer.beginPass(pass, &passAction);
}

//------------------------------------------------------------------------------
void
Gfx::EndPass() {
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->inPass = false;
    state->backend.renderer.endPass();
}

//------------------------------------------------------------------------------
void
Gfx::ApplyDrawState(const DrawState& drawState) {
    o_trace_scoped(Gfx_ApplyDrawState);
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    o_assert_dbg(drawState.Pipeline.Type == GfxResourceType::Pipeline);
    state->gfxFrameInfo.NumApplyDrawState++;

    // apply pipeline and meshes
    pipeline* pip = state->backend.resourceContainer.lookupPipeline(drawState.Pipeline);
    o_assert_dbg(pip);
    mesh* meshes[GfxConfig::MaxNumInputMeshes] = { };
    int numMeshes = 0;
    for (; numMeshes < GfxConfig::MaxNumInputMeshes; numMeshes++) {
        if (drawState.Mesh[numMeshes].IsValid()) {
            meshes[numMeshes] = state->backend.resourceContainer.lookupMesh(drawState.Mesh[numMeshes]);
        }
        else {
            break;
        }
    }
    #if ORYOL_DEBUG
    validateMeshes(pip, meshes, numMeshes);
    #endif
    state->backend.renderer.applyDrawState(pip, meshes, numMeshes);

    // apply vertex textures if any
    texture* vsTextures[GfxConfig::MaxNumVertexTextures] = { };
    int numVSTextures = 0;
    for (; numVSTextures < GfxConfig::MaxNumVertexTextures; numVSTextures++) {
        const Id& texId = drawState.VSTexture[numVSTextures];
        if (texId.IsValid()) {
            vsTextures[numVSTextures] = state->backend.resourceContainer.lookupTexture(texId);
        }
        else {
            break;
        }
    }
    if (numVSTextures > 0) {
        #if ORYOL_DEBUG
        validateTextures(ShaderStage::VS, pip, vsTextures, numVSTextures);
        #endif
        state->backend.renderer.applyTextures(ShaderStage::VS, vsTextures, numVSTextures);
    }

    // apply fragment textures if any
    texture* fsTextures[GfxConfig::MaxNumFragmentTextures] = { };
    int numFSTextures = 0;
    for (; numFSTextures < GfxConfig::MaxNumFragmentTextures; numFSTextures++) {
        const Id& texId = drawState.FSTexture[numFSTextures];
        if (texId.IsValid()) {
            fsTextures[numFSTextures] = state->backend.resourceContainer.lookupTexture(texId);
        }
        else {
            break;
        }
    }
    if (numFSTextures > 0) {
        #if ORYOL_DEBUG
        validateTextures(ShaderStage::FS, pip, fsTextures, numFSTextures);
        #endif
        state->backend.renderer.applyTextures(ShaderStage::FS, fsTextures, numFSTextures);
    }
}

//------------------------------------------------------------------------------
bool
Gfx::QueryFeature(GfxFeature::Code feat) {
    o_assert_dbg(IsValid());
    return state->backend.QueryFeature(feat);
}

//------------------------------------------------------------------------------
ResourceLabel
Gfx::PushResourceLabel() {
    o_assert_dbg(IsValid());
    return state->backend.PushResourceLabel();
}

//------------------------------------------------------------------------------
void
Gfx::PushResourceLabel(ResourceLabel label) {
    o_assert_dbg(IsValid());
    state->backend.PushResourceLabel(label);
}

//------------------------------------------------------------------------------
ResourceLabel
Gfx::PopResourceLabel() {
    o_assert_dbg(IsValid());
    return state->backend.PopResourceLabel();
}

//------------------------------------------------------------------------------
Id
Gfx::LoadResource(const Ptr<ResourceLoader>& loader) {
    o_assert_dbg(IsValid());
    return state->backend.resourceContainer.Load(loader);
}

//------------------------------------------------------------------------------
Id
Gfx::LookupResource(const Locator& locator) {
    o_assert_dbg(IsValid());
    return state->backend.LookupResource(locator);
}

//------------------------------------------------------------------------------
int
Gfx::QueryFreeResourceSlots(GfxResourceType::Code resourceType) {
    o_assert_dbg(IsValid());
    return state->backend.resourceContainer.QueryFreeSlots(resourceType);
}

//------------------------------------------------------------------------------
ResourceInfo
Gfx::QueryResourceInfo(const Id& id) {
    o_assert_dbg(IsValid());
    return state->backend.resourceContainer.QueryResourceInfo(id);
}

//------------------------------------------------------------------------------
ResourcePoolInfo
Gfx::QueryResourcePoolInfo(GfxResourceType::Code resType) {
    o_assert_dbg(IsValid());
    return state->backend.resourceContainer.QueryPoolInfo(resType);
}

//------------------------------------------------------------------------------
void
Gfx::DestroyResources(ResourceLabel label) {
    o_assert_dbg(IsValid());
    return state->backend.DestroyResources(label);
}

//------------------------------------------------------------------------------
_priv::gfxResourceContainer*
Gfx::resource() {
    o_assert_dbg(IsValid());
    return &(state->backend.resourceContainer);
}

//------------------------------------------------------------------------------
void
Gfx::ApplyViewPort(int x, int y, int width, int height, bool originTopLeft) {
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumApplyViewPort++;
    state->backend.ApplyViewPort(x, y, width, height, originTopLeft);
}

//------------------------------------------------------------------------------
void
Gfx::ApplyScissorRect(int x, int y, int width, int height, bool originTopLeft) {
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumApplyScissorRect++;
    state->backend.ApplyScissorRect(x, y, width, height, originTopLeft);
}

//------------------------------------------------------------------------------
void
Gfx::CommitFrame() {
    o_trace_scoped(Gfx_CommitFrame);
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->backend.CommitFrame();
    state->gfxFrameInfo = GfxFrameInfo();
}

//------------------------------------------------------------------------------
void
Gfx::ResetStateCache() {
    o_trace_scoped(Gfx_ResetStateCache);
    o_assert_dbg(IsValid());
    state->backend.ResetStateCache();
}

//------------------------------------------------------------------------------
void
Gfx::UpdateVertices(const Id& id, const void* data, int numBytes) {
    o_trace_scoped(Gfx_UpdateVertices);
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumUpdateVertices++;
    mesh* msh = state->backend.resourceContainer.lookupMesh(id);
    state->backend.renderer.updateVertices(msh, data, numBytes);
}

//------------------------------------------------------------------------------
void
Gfx::UpdateIndices(const Id& id, const void* data, int numBytes) {
    o_trace_scoped(Gfx_UpdateIndices);
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumUpdateIndices++;
    mesh* msh = state->backend.resourceContainer.lookupMesh(id);
    state->backend.renderer.updateIndices(msh, data, numBytes);
}

//------------------------------------------------------------------------------
void
Gfx::UpdateTexture(const Id& id, const void* data, const ImageDataAttrs& offsetsAndSizes) {
    o_trace_scoped(Gfx_UpdateTexture);
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumUpdateTextures++;
    texture* tex = state->backend.resourceContainer.lookupTexture(id);
    state->backend.renderer.updateTexture(tex, data, offsetsAndSizes);
}

//------------------------------------------------------------------------------
void
Gfx::Draw(int primGroupIndex, int numInstances) {
    o_trace_scoped(Gfx_Draw);
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumDraw++;
    state->backend.renderer.draw(primGroupIndex, numInstances);
}

//------------------------------------------------------------------------------
void
Gfx::Draw(const PrimitiveGroup& primGroup, int numInstances) {
    o_trace_scoped(Gfx_Draw);
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumDraw++;
    state->backend.renderer.draw(primGroup.BaseElement, primGroup.NumElements, numInstances);
}

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateMeshes(pipeline* pip, mesh** meshes, int num) {

    // checks that:
    //  - at least one input mesh must be attached, and it must be in slot 0
    //  - all attached input meshes must be valid
    //  - PrimitivesGroups may only be attached to the mesh in slot 0
    //  - if indexed rendering is used, the mesh in slot 0 must have an index buffer
    //  - the mesh in slot 0 cannot contain per-instance-data
    //  - no colliding vertex attributes across all input meshes
    //
    // this method should only be called in debug mode

    // FIXME FIXME FIXME: check for matching vertex layout!!!

    o_assert_dbg(meshes && (num > 0) && (num < GfxConfig::MaxNumInputMeshes));
    if (nullptr == meshes[0]) {
        o_error("invalid mesh block: at least one input mesh must be provided, in slot 0!\n");
    }
    if ((meshes[0]->indexBufferAttrs.Type != IndexType::None) &&
        (meshes[0]->indexBufferAttrs.NumIndices == 0)) {
        o_error("invalid mesh block: the input mesh at slot 0 uses indexed rendering, but has no indices!\n");
    }

    StaticArray<int, VertexAttr::NumVertexAttrs> vertexAttrCounts;
    vertexAttrCounts.Fill(0);
    for (int mshIndex = 0; mshIndex < GfxConfig::MaxNumInputMeshes; mshIndex++) {
        const meshBase* msh = meshes[mshIndex];
        if (msh) {
            if (ResourceState::Valid != msh->State) {
                o_error("invalid mesh block: input mesh at slot '%d' not valid!\n", mshIndex);
            }
            if ((mshIndex > 0) && (msh->indexBufferAttrs.Type != IndexType::None)) {
                o_error("invalid drawState: input mesh at slot '%d' has indices, only allowed for slot 0!\n", mshIndex);
            }
            if ((mshIndex > 0) && (msh->numPrimGroups > 0)) {
                o_error("invalid mesh block: input mesh at slot '%d' has primitive groups, only allowed for slot 0!\n", mshIndex);
            }
            const int numComps = msh->vertexBufferAttrs.Layout.NumComponents();
            for (int compIndex = 0; compIndex < numComps; compIndex++) {
                const auto& comp = msh->vertexBufferAttrs.Layout.ComponentAt(compIndex);
                vertexAttrCounts[comp.Attr]++;
                if (vertexAttrCounts[comp.Attr] > 1) {
                    o_error("invalid mesh block: same vertex attribute declared in multiple input meshes!\n");
                }
            }
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateTextures(ShaderStage::Code stage, pipeline* pip, texture** textures, int numTextures) {
    o_assert_dbg(pip);

    // check if provided texture types are compatible with the expections shader
    const shader* shd = pip->shd;
    o_assert_dbg(shd);
    for (int slot = 0; slot < numTextures; slot++) {
        int index = shd->Setup.TextureIndexByStageAndSlot(stage, slot);
        if ((InvalidIndex != index) && textures[slot]) {
            if (textures[slot]->textureAttrs.Type != shd->Setup.TexType(index)) {
                o_error("Texture type mismatch at slot '%d'!\n", slot);
            }
        }
        else if ((InvalidIndex == index) && textures[slot]) {
            o_warn("Texture applied at slot '%d' which isn't expected by shader.\n", slot);
        }
        else if ((InvalidIndex != index) && !textures[slot]) {
            o_error("No texture applied at slot '%d', but shader expects one!\n", slot);
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateTextureSetup(const TextureSetup& setup, const void* data, int size) {
    o_assert((setup.NumMipMaps > 0) && (setup.NumMipMaps <= GfxConfig::MaxNumTextureMipMaps));
    o_assert((setup.Width >= 1) && (setup.Height >= 1) && (setup.Depth >= 1));
    if (data) {
        o_assert(size > 0);
        o_assert(setup.TextureUsage == Usage::Immutable);
        o_assert(setup.ImageData.NumMipMaps == setup.NumMipMaps);
        if (setup.Type == TextureType::TextureCube) {
            o_assert(setup.ImageData.NumFaces == 6);
        }
        else {
            o_assert(setup.ImageData.NumFaces == 1);
        }
    }
    if (setup.Type == TextureType::Texture2D) {
        o_assert(setup.Depth == 1);
    }
    if (setup.Type == TextureType::TextureArray) {
        o_assert(setup.Depth <= GfxConfig::MaxNumTextureArraySlices);
    }
    if (setup.Type == TextureType::Texture3D) {
        o_assert(!setup.IsRenderTarget);
    }
    if (setup.IsRenderTarget) {
        o_assert(setup.TextureUsage == Usage::Immutable);
        o_assert(PixelFormat::IsValidRenderTargetColorFormat(setup.ColorFormat));
        if (setup.DepthFormat != PixelFormat::InvalidPixelFormat) {
            o_assert(PixelFormat::IsValidRenderTargetDepthFormat(setup.DepthFormat));
        }
    }
    else {
        o_assert(setup.SampleCount == 1);
        o_assert(setup.DepthFormat == PixelFormat::InvalidPixelFormat);
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateMeshSetup(const MeshSetup& setup, const void* data, int size) {
    o_assert(setup.ShouldSetupFullScreenQuad() || (setup.VertexUsage != Usage::InvalidUsage) || (setup.IndexUsage != Usage::InvalidUsage));
    if (setup.NumVertices > 0) {
        o_assert(!setup.Layout.Empty());
        if (setup.VertexUsage == Usage::Immutable) {
            o_assert(data && (size > 0));
            o_assert((setup.VertexDataOffset >= 0) && (setup.VertexDataOffset < size));
        }
    }
    if (setup.NumIndices > 0) {
        o_assert((setup.IndicesType == IndexType::Index16) || (setup.IndicesType == IndexType::Index32));
        if (setup.IndexUsage == Usage::Immutable) {
            o_assert(data && (size > 0));
            o_assert((setup.IndexDataOffset >= 0) && (setup.IndexDataOffset < size));
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validatePipelineSetup(const PipelineSetup& setup) {
    o_assert(setup.PrimType != PrimitiveType::InvalidPrimitiveType);
    o_assert(setup.Shader.IsValid());
    bool anyLayoutValid = false;
    for (const auto& layout : setup.Layouts) {
        if (!layout.Empty()) {
            anyLayoutValid = true;
            break;
        }
    }
    o_assert(anyLayoutValid);
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validatePassSetup(const PassSetup& setup) {
    // check that at least one color attachment texture is defined
    // and that there are no 'holes' if there are multiple attachments
    bool continuous = true;
    for (int i = 0; i < GfxConfig::MaxNumColorAttachments; i++) {
        if (setup.ColorAttachments[i].Texture.IsValid()) {
            if (!continuous) {
                o_error("invalid render pass: must have continuous color attachments!\n");
            }
        }
        else {
            if (0 == i) {
                o_error("invalid render pass: must have color attachment at slot 0!\n");
            }
            continuous = false;
        }
    }

    // check that all render targets have the required params
    const texture* t0 = state->backend.resourceContainer.lookupTexture(setup.ColorAttachments[0].Texture);
    o_assert(t0);
    const int w = t0->textureAttrs.Width;
    const int h = t0->textureAttrs.Height;
    const int sampleCount = t0->textureAttrs.SampleCount;
    for (int i = 0; i < GfxConfig::MaxNumColorAttachments; i++) {
        const texture* tex = state->backend.resourceContainer.lookupTexture(setup.ColorAttachments[i].Texture);
        if (tex) {
            const auto& attrs = tex->textureAttrs;
            if ((attrs.Width != w) || (attrs.Height != h)) {
                o_error("invalid render pass: all color attachments must have the same size!\n");
            }
            if (attrs.SampleCount != sampleCount) {
                o_error("invalid render pass: all color attachments must have same sample-count!\n");
            }
            if (attrs.TextureUsage != Usage::Immutable) {
                o_error("invalid render pass: color attachments must have immutable usage!\n");
            }
            if (!tex->Setup.IsRenderTarget) {
                o_error("invalid render pass: color attachment must have been setup as render target!\n");
            }
        }
    }
    const texture* dsTex = state->backend.resourceContainer.lookupTexture(setup.DepthStencilTexture);
    if (dsTex) {
        const auto& attrs = dsTex->textureAttrs;
        if ((attrs.Width != w) || (attrs.Height != h)) {
            o_error("invalid render pass: depth-stencil attachment must have same size as color attachments!\n");
        }
        if (attrs.SampleCount != sampleCount) {
            o_error("invalid render pass: depth-stencil attachment must have sample sample-count as color attachments!\n");
        }
        if (attrs.TextureUsage != Usage::Immutable) {
            o_error("invalid render pass: depth attachment must have immutable usage!\n");
        }
        if (!dsTex->Setup.IsRenderTarget) {
            o_error("invalid render pass: depth attachment must have been setup as render target!\n");
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateShaderSetup(const ShaderSetup& setup) {
    // hmm, FIXME
}
#endif

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const TextureSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateTextureSetup(setup, data, size);
    #endif
    return state->backend.resourceContainer.Create(setup, data, size);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const MeshSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateMeshSetup(setup, data, size);
    #endif
    return state->backend.resourceContainer.Create(setup, data, size);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const ShaderSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateShaderSetup(setup);
    #endif
    return state->backend.resourceContainer.Create(setup, nullptr, 0);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const PipelineSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validatePipelineSetup(setup);
    #endif
    return state->backend.resourceContainer.Create(setup, nullptr, 0);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const PassSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validatePassSetup(setup);
    #endif
    return state->backend.resourceContainer.Create(setup, nullptr, 0);
}

//------------------------------------------------------------------------------
void
Gfx::applyUniformBlock(ShaderStage::Code bindStage, int bindSlot, uint32_t layoutHash, const uint8_t* ptr, int byteSize) {
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumApplyUniformBlock++;
    state->backend.renderer.applyUniformBlock(bindStage, bindSlot, layoutHash, ptr, byteSize);
}

} // namespace Oryol
