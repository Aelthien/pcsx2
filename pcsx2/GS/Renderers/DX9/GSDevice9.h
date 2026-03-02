// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GSTexture9.h"
#include "GS/GSVector.h"
#include "GS/Renderers/Common/GSDevice.h"

#include <d3d9.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include <string_view>

// Vertex format for hardware rendering with lighting support
#pragma pack(push, 1)
struct GSVertexDX9
{
	float x, y, z;      // Position (world space - will be transformed by matrices)
	float nx, ny, nz;   // Normal for lighting
	DWORD color;        // Diffuse color (ARGB)
	float u, v;         // Texture coordinates
};
#pragma pack(pop)

#define GSVERTEXDX9_FVF (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// Simple vertex for present/convert operations
struct GSVertexPT1DX9
{
	float x, y, z, rhw;
	float u, v;
};
#define GSVERTEXPT1DX9_FVF (D3DFVF_XYZRHW | D3DFVF_TEX1)

class GSDevice9 final : public GSDevice
{
public:
	using VSSelector = GSHWDrawConfig::VSSelector;
	using PSSelector = GSHWDrawConfig::PSSelector;
	using PSSamplerSelector = GSHWDrawConfig::SamplerSelector;
	using OMDepthStencilSelector = GSHWDrawConfig::DepthStencilSelector;
	using OMBlendSelector = GSHWDrawConfig::BlendState;

private:
	enum : u32
	{
		MAX_TEXTURES = 4,
		MAX_SAMPLERS = 4,
		VERTEX_BUFFER_SIZE = 4 * 1024 * 1024,
		INDEX_BUFFER_SIZE = 2 * 1024 * 1024,
	};

	// Core D3D9 objects
	IDirect3D9* m_d3d = nullptr;
	IDirect3DDevice9* m_dev = nullptr;
	D3DPRESENT_PARAMETERS m_pp = {};
	D3DCAPS9 m_caps = {};

	// Buffers
	IDirect3DVertexBuffer9* m_vb = nullptr;
	IDirect3DIndexBuffer9* m_ib = nullptr;
	u32 m_vb_pos = 0;
	u32 m_ib_pos = 0;

	// State tracking
	struct
	{
		IDirect3DVertexDeclaration9* vertex_decl;
		IDirect3DVertexBuffer9* vertex_buffer;
		IDirect3DIndexBuffer9* index_buffer;
		u32 vb_stride;
		IDirect3DTexture9* textures[MAX_TEXTURES];
		IDirect3DSurface9* rt;
		IDirect3DSurface9* ds;
		D3DVIEWPORT9 viewport;
		RECT scissor;
		bool scissor_enabled;
	} m_state = {};

	// Vertex declarations
	IDirect3DVertexDeclaration9* m_vertex_decl_hw = nullptr;
	IDirect3DVertexDeclaration9* m_vertex_decl_pt1 = nullptr;

	// Convert/present resources
	struct
	{
		IDirect3DVertexDeclaration9* vdecl;
		IDirect3DPixelShader9* ps[static_cast<int>(ShaderConvert::Count)];
		IDirect3DVertexShader9* vs;
	} m_convert = {};

	struct
	{
		IDirect3DPixelShader9* ps[static_cast<int>(PresentShader::Count)];
	} m_present = {};

	// Default depth buffer for rendering (backbuffer size)
	IDirect3DSurface9* m_default_ds = nullptr;
	
	// Dynamic depth buffer that matches current RT size
	IDirect3DSurface9* m_rt_ds = nullptr;
	u32 m_rt_ds_width = 0;
	u32 m_rt_ds_height = 0;
	
// Track if we've rendered directly to backbuffer this frame (for RTX Remix)
	bool m_rendered_to_backbuffer = false;
	bool m_in_scene = false;

	// VU1 memory capture for skeletal/pre-transform data extraction
	struct VU1Capture
	{
		bool enabled = false;
		bool hasData = false;
		float mvpMatrix[16] = {};
		float boneMatrices[64][16] = {}; // Up to 64 bones
		u32 boneCount = 0;
		u8 rawMemory[16384] = {}; // Full VU1 memory snapshot
	} m_vu1Capture;

	// Cached states
	std::unordered_map<u32, IDirect3DPixelShader9*> m_ps_cache;
	std::unordered_map<u32, IDirect3DVertexShader9*> m_vs_cache;

	// Helper functions
	bool CreateDevice();
	void DestroyDevice();
	bool CreateBuffers();
	void DestroyBuffers();
	bool CreateVertexDeclarations();
	void DestroyVertexDeclarations();
	bool CreateConvertShaders();
	void DestroyConvertShaders();

	void SetupDATE(GSTexture* rt, GSTexture* ds, SetDATM datm, const GSVector4i& bbox);

	// State setup
	void SetupVS(VSSelector sel, const GSHWDrawConfig::VSConstantBuffer* cb);
	void SetupPS(const PSSelector& sel, const GSHWDrawConfig::PSConstantBuffer* cb, PSSamplerSelector ssel);
	void SetupOM(OMDepthStencilSelector dssel, OMBlendSelector bsel, u8 afix);

	// Texture state
	void PSSetShaderResource(int i, GSTexture* sr);
	void PSSetSamplerState(int i, PSSamplerSelector sel);

	// Render target
	void OMSetRenderTargets(GSTexture* rt, GSTexture* ds, const GSVector4i* scissor = nullptr);

protected:
	GSTexture* CreateSurface(GSTexture::Type type, int width, int height, int levels, GSTexture::Format format) override;

	void DoMerge(GSTexture* sTex[3], GSVector4* sRect, GSTexture* dTex, GSVector4* dRect, const GSRegPMODE& PMODE, const GSRegEXTBUF& EXTBUF, u32 c, const bool linear) override;
	void DoInterlace(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect, ShaderInterlace shader, bool linear, const InterlaceConstantBuffer& cb) override;
	void DoFXAA(GSTexture* sTex, GSTexture* dTex) override;
	void DoShadeBoost(GSTexture* sTex, GSTexture* dTex, const float params[4]) override;
	bool DoCAS(GSTexture* sTex, GSTexture* dTex, bool sharpen_only, const std::array<u32, NUM_CAS_CONSTANTS>& constants) override;

	void DrawStretchRect(const GSVector4& sRect, const GSVector4& dRect, const GSVector2i& ds);
	void DoStretchRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
		GSHWDrawConfig::ColorMaskSelector cms, ShaderConvert shader, bool linear) override;

public:
	GSDevice9();
	~GSDevice9() override;

	__fi static GSDevice9* GetInstance() { return static_cast<GSDevice9*>(g_gs_device.get()); }
	__fi IDirect3DDevice9* GetD3DDevice() const { return m_dev; }

	bool Create(GSVSyncMode vsync_mode, bool allow_present_throttle) override;
	void Destroy() override;

	RenderAPI GetRenderAPI() const override;

	bool UpdateWindow() override;
	void ResizeWindow(u32 new_window_width, u32 new_window_height, float new_window_scale) override;
	bool SupportsExclusiveFullscreen() const override;
	bool HasSurface() const override;
	void DestroySurface() override;
	std::string GetDriverInfo() const override;

	void SetVSyncMode(GSVSyncMode mode, bool allow_present_throttle) override;

	PresentResult BeginPresent(bool frame_skip) override;
	void EndPresent() override;

	bool SetGPUTimingEnabled(bool enabled) override;
	float GetAndResetAccumulatedGPUTime() override;

	void PushDebugGroup(const char* fmt, ...) override;
	void PopDebugGroup() override;
	void InsertDebugMessage(DebugMessageCategory category, const char* fmt, ...) override;

	void RenderImGui();

	std::unique_ptr<GSDownloadTexture> CreateDownloadTexture(u32 width, u32 height, GSTexture::Format format) override;

	void CopyRect(GSTexture* sTex, GSTexture* dTex, const GSVector4i& r, u32 destX, u32 destY) override;
	void PresentRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect, PresentShader shader, float shaderTime, bool linear) override;
	void UpdateCLUTTexture(GSTexture* sTex, float sScale, u32 offsetX, u32 offsetY, GSTexture* dTex, u32 dOffset, u32 dSize) override;
	void ConvertToIndexedTexture(GSTexture* sTex, float sScale, u32 offsetX, u32 offsetY, u32 SBW, u32 SPSM, GSTexture* dTex, u32 DBW, u32 DPSM) override;
	void FilteredDownsampleTexture(GSTexture* sTex, GSTexture* dTex, u32 downsample_factor, const GSVector2i& clamp_min, const GSVector4& dRect) override;

	void RenderHW(GSHWDrawConfig& config) override;

	void ClearSamplerCache() override;

	// VU1 memory capture for extracting pre-transform data
	void EnableVU1Capture(bool enable);
	void CaptureVU1Memory(const u8* vu1Mem, u32 size);
	bool HasVU1CaptureData() const { return m_vu1Capture.hasData; }
	const float* GetCapturedMVPMatrix() const { return m_vu1Capture.mvpMatrix; }

	// Vertex/Index buffer management
	void* IAMapVertexBuffer(u32 stride, u32 count);
	void IAUnmapVertexBuffer(u32 stride, u32 count);
	bool IASetVertexBuffer(const void* vertex, u32 stride, u32 count);

	u16* IAMapIndexBuffer(u32 count);
	void IAUnmapIndexBuffer(u32 count);
	bool IASetIndexBuffer(const void* index, u32 count);

	void IASetPrimitiveTopology(D3DPRIMITIVETYPE topology);
	void IASetVertexDeclaration(IDirect3DVertexDeclaration9* decl);

	// Draw commands
	void DrawPrimitive();
	void DrawIndexedPrimitive();
	void DrawIndexedPrimitive(int offset, int count);
};
