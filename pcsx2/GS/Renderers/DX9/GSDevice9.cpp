// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSDevice9.h"
#include "VU1MemoryLayoutConfig.h"
#include "VIFUnpackCapture.h"
#include "GS/GSGL.h"
#include "GS/GSPerfMon.h"
#include "GS/GSUtil.h"
#include "GS/Renderers/Common/GSVertex.h"
#include "Host.h"
#include "VMManager.h"

#include "common/Console.h"

#include "imgui.h"

#include <cmath>
#include <d3d9.h>
#include <vector>

GSDevice9::GSDevice9()
{
	std::memset(&m_state, 0, sizeof(m_state));
	m_features.dxt_textures = true;
	m_features.stencil_buffer = true;
}

GSDevice9::~GSDevice9()
{
	Destroy();
}

RenderAPI GSDevice9::GetRenderAPI() const
{
	return RenderAPI::D3D9;
}

bool GSDevice9::Create(GSVSyncMode vsync_mode, bool allow_present_throttle)
{
	if (!GSDevice::Create(vsync_mode, allow_present_throttle))
		return false;

	// Allocate a console for debug output
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
	printf("DX9 Debug Console\n");

	m_d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!m_d3d)
		return false;

	D3DADAPTER_IDENTIFIER9 adapter_id = {};
	if (SUCCEEDED(m_d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &adapter_id)))
		m_name = adapter_id.Description;

	if (FAILED(m_d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &m_caps)))
		return false;

	if (!AcquireWindow(true))
		return false;

	if (!CreateDevice())
		return false;

	if (!CreateBuffers())
		return false;

	if (!CreateVertexDeclarations())
		return false;

	m_max_texture_size = std::min<u32>(m_caps.MaxTextureWidth, m_caps.MaxTextureHeight);

	// Auto-enable VU1 capture for pre-transform vertex data extraction
	EnableVU1Capture(true);

	return true;
}

void GSDevice9::Destroy()
{
	// Disable VU1 capture
	EnableVU1Capture(false);

	DestroyVertexDeclarations();
	DestroyBuffers();
	DestroyDevice();

	if (m_d3d)
	{
		m_d3d->Release();
		m_d3d = nullptr;
	}

	GSDevice::Destroy();
}

bool GSDevice9::CreateDevice()
{
	ZeroMemory(&m_pp, sizeof(m_pp));
	m_pp.BackBufferWidth = m_window_info.surface_width;
	m_pp.BackBufferHeight = m_window_info.surface_height;
	m_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	m_pp.BackBufferCount = 1;
	m_pp.SwapEffect = D3DSWAPEFFECT_COPY;
	m_pp.hDeviceWindow = reinterpret_cast<HWND>(m_window_info.window_handle);
	m_pp.Windowed = TRUE;
	m_pp.PresentationInterval = (m_vsync_mode == GSVSyncMode::FIFO) ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

	DWORD behavior = (m_caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? D3DCREATE_HARDWARE_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	
	return SUCCEEDED(m_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_pp.hDeviceWindow, behavior, &m_pp, &m_dev));
}

void GSDevice9::DestroyDevice()
{
	if (m_dev)
	{
		m_dev->Release();
		m_dev = nullptr;
	}
}

bool GSDevice9::CreateBuffers()
{
	if (FAILED(m_dev->CreateVertexBuffer(VERTEX_BUFFER_SIZE, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &m_vb, nullptr)))
		return false;

	if (FAILED(m_dev->CreateIndexBuffer(INDEX_BUFFER_SIZE * sizeof(u16), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_ib, nullptr)))
		return false;

	// Create default depth buffer matching backbuffer size
	if (FAILED(m_dev->CreateDepthStencilSurface(m_pp.BackBufferWidth, m_pp.BackBufferHeight, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &m_default_ds, nullptr)))
		return false;

	return true;
}

void GSDevice9::DestroyBuffers()
{
	if (m_rt_ds)
	{
		m_rt_ds->Release();
		m_rt_ds = nullptr;
		m_rt_ds_width = 0;
		m_rt_ds_height = 0;
	}
	if (m_default_ds)
	{
		m_default_ds->Release();
		m_default_ds = nullptr;
	}
	if (m_ib)
	{
		m_ib->Release();
		m_ib = nullptr;
	}
	if (m_vb)
	{
		m_vb->Release();
		m_vb = nullptr;
	}
}

bool GSDevice9::CreateVertexDeclarations()
{
	D3DVERTEXELEMENT9 elements_hw[] = {
		{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0, 12, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
		{0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
		{0, 20, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
		{0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
		D3DDECL_END()
	};

	if (FAILED(m_dev->CreateVertexDeclaration(elements_hw, &m_vertex_decl_hw)))
		return false;

	D3DVERTEXELEMENT9 elements_pt1[] = {
		{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
		D3DDECL_END()
	};

	if (FAILED(m_dev->CreateVertexDeclaration(elements_pt1, &m_vertex_decl_pt1)))
		return false;

	return true;
}

void GSDevice9::DestroyVertexDeclarations()
{
	if (m_vertex_decl_pt1)
	{
		m_vertex_decl_pt1->Release();
		m_vertex_decl_pt1 = nullptr;
	}
	if (m_vertex_decl_hw)
	{
		m_vertex_decl_hw->Release();
		m_vertex_decl_hw = nullptr;
	}
}

bool GSDevice9::CreateConvertShaders()
{
	// TODO: Implement shader creation
	return true;
}

void GSDevice9::DestroyConvertShaders()
{
	// TODO: Implement shader destruction
}

GSTexture* GSDevice9::CreateSurface(GSTexture::Type type, int width, int height, int levels, GSTexture::Format format)
{
	D3DFORMAT d3dfmt = GSTexture9::GetD3D9Format(format);
	
	if (type == GSTexture::Type::DepthStencil)
	{
		// Depth stencil must be a surface
		IDirect3DSurface9* surface = nullptr;
		HRESULT hr = m_dev->CreateDepthStencilSurface(width, height, d3dfmt, D3DMULTISAMPLE_NONE, 0, FALSE, &surface, nullptr);
		if (FAILED(hr))
			return nullptr;

		D3DSURFACE_DESC desc;
		surface->GetDesc(&desc);
		return new GSTexture9(surface, desc, type, format);
	}
	else if (type == GSTexture::Type::RenderTarget)
	{
		// Create as texture with D3DUSAGE_RENDERTARGET so it can be both rendered to AND sampled from
		IDirect3DTexture9* texture = nullptr;
		HRESULT hr = m_dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, d3dfmt, D3DPOOL_DEFAULT, &texture, nullptr);
		if (FAILED(hr))
			return nullptr;

		D3DSURFACE_DESC desc;
		texture->GetLevelDesc(0, &desc);
		return new GSTexture9(texture, desc, type, format);
	}
	else
	{
		// Regular texture
		IDirect3DTexture9* texture = nullptr;
		if (FAILED(m_dev->CreateTexture(width, height, levels, 0, d3dfmt, D3DPOOL_MANAGED, &texture, nullptr)))
			return nullptr;

		D3DSURFACE_DESC desc;
		texture->GetLevelDesc(0, &desc);
		return new GSTexture9(texture, desc, type, format);
	}
}

std::unique_ptr<GSDownloadTexture> GSDevice9::CreateDownloadTexture(u32 width, u32 height, GSTexture::Format format)
{
	return GSDownloadTexture9::Create(m_dev, width, height, format);
}

void GSDevice9::CopyRect(GSTexture* sTex, GSTexture* dTex, const GSVector4i& r, u32 destX, u32 destY)
{
	if (!sTex || !dTex || !m_dev)
		return;

	GSTexture9* sTex9 = static_cast<GSTexture9*>(sTex);
	GSTexture9* dTex9 = static_cast<GSTexture9*>(dTex);

	IDirect3DSurface9* sSurf = sTex9->GetSurface();
	IDirect3DSurface9* dSurf = dTex9->GetSurface();

	if (!sSurf || !dSurf)
		return;

	RECT srcRect = {r.x, r.y, r.z, r.w};
	RECT dstRect = {(LONG)destX, (LONG)destY, (LONG)(destX + r.width()), (LONG)(destY + r.height())};

	m_dev->StretchRect(sSurf, &srcRect, dSurf, &dstRect, D3DTEXF_POINT);
}

void GSDevice9::DrawStretchRect(const GSVector4& sRect, const GSVector4& dRect, const GSVector2i& ds)
{
	// Use perspective projection for RTX Remix camera compatibility
	const float left = dRect.x;
	const float top = dRect.y;
	const float right = dRect.z;
	const float bottom = dRect.w;

	// Set up perspective projection matching RenderHW
	float camDist = 500.0f;
	float fovY = 2.0f * atanf((ds.y * 0.5f) / camDist);
	float aspect = (float)ds.x / (float)ds.y;
	float zn = 1.0f, zf = 10000.0f;
	float yScale = 1.0f / tanf(fovY * 0.5f);
	float xScale = yScale / aspect;

	D3DMATRIX identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
	D3DMATRIX proj = {
		xScale, 0, 0, 0,
		0, yScale, 0, 0,
		0, 0, zf / (zf - zn), 1,
		0, 0, -zn * zf / (zf - zn), 0
	};
	m_dev->SetTransform(D3DTS_WORLD, &identity);
	m_dev->SetTransform(D3DTS_VIEW, &identity);
	m_dev->SetTransform(D3DTS_PROJECTION, &proj);

	// Convert screen coords to world space (matching RenderHW)
	// NDC: map [0,ds] to [-1,1]
	float ndc_left = (left / ds.x) * 2.0f - 1.0f;
	float ndc_right = (right / ds.x) * 2.0f - 1.0f;
	float ndc_top = 1.0f - (top / ds.y) * 2.0f;
	float ndc_bottom = 1.0f - (bottom / ds.y) * 2.0f;

	// World space coords
	float world_left = ndc_left * (ds.x * 0.5f);
	float world_right = ndc_right * (ds.x * 0.5f);
	float world_top = ndc_top * (ds.y * 0.5f);
	float world_bottom = ndc_bottom * (ds.y * 0.5f);

	// Vertex with position, normal, color, texcoord
	struct VertexPT1 {
		float x, y, z;
		float nx, ny, nz;
		DWORD color;
		float u, v;
	};

	VertexPT1 vertices[4] = {
		{world_left,  world_top,    camDist, 0,0,-1, D3DCOLOR_XRGB(255,255,255), sRect.x, sRect.y},
		{world_right, world_top,    camDist, 0,0,-1, D3DCOLOR_XRGB(255,255,255), sRect.z, sRect.y},
		{world_left,  world_bottom, camDist, 0,0,-1, D3DCOLOR_XRGB(255,255,255), sRect.x, sRect.w},
		{world_right, world_bottom, camDist, 0,0,-1, D3DCOLOR_XRGB(255,255,255), sRect.z, sRect.w},
	};

	m_dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	m_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VertexPT1));
}

void GSDevice9::DoStretchRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	GSHWDrawConfig::ColorMaskSelector cms, ShaderConvert shader, bool linear)
{
	// Skip offscreen RT operations - we render directly to backbuffer for RTX Remix
	return;

	if (!sTex || !dTex || !m_dev)
		return;

	GSTexture9* sTex9 = static_cast<GSTexture9*>(sTex);
	GSTexture9* dTex9 = static_cast<GSTexture9*>(dTex);

	IDirect3DSurface9* dSurf = dTex9->GetSurface();
	IDirect3DTexture9* sTexD3D = sTex9->GetTexture();

	if (!dSurf)
		return;

	// Set render target
	m_dev->SetRenderTarget(0, dSurf);
	m_dev->SetDepthStencilSurface(nullptr);

	// Set viewport
	D3DVIEWPORT9 vp = {};
	vp.Width = dTex->GetWidth();
	vp.Height = dTex->GetHeight();
	vp.MaxZ = 1.0f;
	m_dev->SetViewport(&vp);

	// Set texture and sampler
	if (sTexD3D)
	{
		m_dev->SetTexture(0, sTexD3D);
		m_dev->SetSamplerState(0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
		m_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
		m_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		m_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}

	// Setup fixed function pipeline for textured rendering
	m_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	m_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	m_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

	// Draw quad
	DrawStretchRect(sRect, dRect, GSVector2i(dTex->GetWidth(), dTex->GetHeight()));

	m_dev->SetTexture(0, nullptr);
}

void GSDevice9::DoMerge(GSTexture* sTex[3], GSVector4* sRect, GSTexture* dTex, GSVector4* dRect,
	const GSRegPMODE& PMODE, const GSRegEXTBUF& EXTBUF, u32 c, const bool linear)
{
	// Skip merge - we render directly to backbuffer for RTX Remix
	return;

	if (!dTex || !m_dev)
		return;

	GSTexture9* dTex9 = static_cast<GSTexture9*>(dTex);
	IDirect3DSurface9* dSurf = dTex9->GetSurface();
	if (!dSurf)
		return;

	// Set destination render target
	m_dev->SetRenderTarget(0, dSurf);
	m_dev->SetDepthStencilSurface(nullptr);

	// Clear destination
	m_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

	// Set viewport
	D3DVIEWPORT9 vp = {};
	vp.Width = dTex->GetWidth();
	vp.Height = dTex->GetHeight();
	vp.MaxZ = 1.0f;
	m_dev->SetViewport(&vp);

	// Fixed function setup
	m_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	m_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	m_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

	// Simple merge: draw the first available source as a textured quad
	for (int i = 0; i < 2; i++)
	{
		if (sTex[i])
		{
			GSTexture9* sTex9 = static_cast<GSTexture9*>(sTex[i]);
			IDirect3DTexture9* sTexD3D = sTex9->GetTexture();
			if (sTexD3D)
			{
				m_dev->SetTexture(0, sTexD3D);
				m_dev->SetSamplerState(0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
				m_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
				m_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
				m_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

				DrawStretchRect(sRect[i], dRect[i], GSVector2i(dTex->GetWidth(), dTex->GetHeight()));
			}
		}
	}

	m_dev->SetTexture(0, nullptr);
}

void GSDevice9::DoInterlace(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	ShaderInterlace shader, bool linear, const InterlaceConstantBuffer& cb)
{
	// Skip interlace - we render directly to backbuffer for RTX Remix
	return;
}

void GSDevice9::DoFXAA(GSTexture* sTex, GSTexture* dTex)
{
	// D3D9 doesn't support FXAA easily
}

void GSDevice9::DoShadeBoost(GSTexture* sTex, GSTexture* dTex, const float params[4])
{
	// TODO: Implement shade boost
}

bool GSDevice9::DoCAS(GSTexture* sTex, GSTexture* dTex, bool sharpen_only, const std::array<u32, NUM_CAS_CONSTANTS>& constants)
{
	return false; // CAS not supported in D3D9
}

void GSDevice9::PresentRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	PresentShader shader, float shaderTime, bool linear)
{
	if (!m_dev)
		return;

	// For RTX Remix: ALWAYS skip presenting to the backbuffer
	// We render geometry directly to backbuffer in RenderHW, so PresentRect would
	// overwrite our 3D geometry with a 2D textured quad (which Remix can't raytrace)
	// The PS2 uses interlaced rendering, so RenderHW is only called every other frame,
	// but we still want to skip PresentRect on all frames to avoid flickering
	if (!dTex)
		return;

	// Get destination surface
	IDirect3DSurface9* dSurf = nullptr;
	int dWidth, dHeight;

	if (dTex)
	{
		GSTexture9* dTex9 = static_cast<GSTexture9*>(dTex);
		dSurf = dTex9->GetSurface();
		dWidth = dTex->GetWidth();
		dHeight = dTex->GetHeight();
	}
	else
	{
		m_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &dSurf);
		dWidth = m_window_info.surface_width;
		dHeight = m_window_info.surface_height;
	}

	if (!dSurf)
		return;

	// Set render target
	m_dev->SetRenderTarget(0, dSurf);
	m_dev->SetDepthStencilSurface(nullptr);

	// Set viewport
	D3DVIEWPORT9 vp = {};
	vp.Width = dWidth;
	vp.Height = dHeight;
	vp.MaxZ = 1.0f;
	m_dev->SetViewport(&vp);

	// Fixed function setup
	m_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	m_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	// Check if we have a valid source texture
	IDirect3DTexture9* sTexD3D = nullptr;
	if (sTex)
	{
		GSTexture9* sTex9 = static_cast<GSTexture9*>(sTex);
		sTexD3D = sTex9->GetTexture();
	}

	if (sTexD3D)
	{
		// Draw textured quad
		m_dev->SetTexture(0, sTexD3D);
		m_dev->SetSamplerState(0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
		m_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
		m_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		m_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		DrawStretchRect(sRect, dRect, GSVector2i(dWidth, dHeight));
		m_dev->SetTexture(0, nullptr);
	}

	// Release backbuffer reference if we acquired it
	if (!dTex)
		dSurf->Release();
}

void GSDevice9::UpdateCLUTTexture(GSTexture* sTex, float sScale, u32 offsetX, u32 offsetY, GSTexture* dTex, u32 dOffset, u32 dSize)
{
	// TODO: Implement CLUT update
}

void GSDevice9::ConvertToIndexedTexture(GSTexture* sTex, float sScale, u32 offsetX, u32 offsetY, u32 SBW, u32 SPSM, GSTexture* dTex, u32 DBW, u32 DPSM)
{
	// TODO: Implement indexed texture conversion
}

void GSDevice9::FilteredDownsampleTexture(GSTexture* sTex, GSTexture* dTex, u32 downsample_factor, const GSVector2i& clamp_min, const GSVector4& dRect)
{
	// TODO: Implement filtered downsample
}

void GSDevice9::RenderHW(GSHWDrawConfig& config)
{
	if (!config.verts || config.nverts == 0 || !m_dev)
		return;

	// Enable VU1 capture on first call
	static bool captureEnabled = false;
	if (!captureEnabled)
	{
		VU1InputCapture::GetInstance().SetEnabled(true);
		captureEnabled = true;
	}

	// Get pending VU1 vertex snapshots (will be rendered after setup)
	std::vector<VU1InputSnapshot> vu1Snapshots = VU1InputCapture::GetInstance().GetAndClearSnapshots();

	// Debug: track texture usage
	static int frame_count = 0;
	static int tex_null_count = 0;
	static int tex_valid_count = 0;
	static int d3dtex_null_count = 0;
	frame_count++;
	if (config.tex)
	{
		tex_valid_count++;
		GSTexture9* tex9 = static_cast<GSTexture9*>(config.tex);
		if (!tex9->GetTexture())
			d3dtex_null_count++;
	}
	else
		tex_null_count++;

	// D3D9 requires draw calls between BeginScene/EndScene
	// RenderHW is called before BeginPresent, so we need to start a scene here
	if (!m_in_scene)
	{
		if (FAILED(m_dev->BeginScene()))
			return;
		m_in_scene = true;
	}

	// Clear at start of RenderHW to prevent overlapping, but only once per frame
	// This is better than clearing in BeginPresent because RenderHW isn't called every frame (interlacing)
	if (!m_rendered_to_backbuffer)
	{
		IDirect3DSurface9* bb = nullptr;
		if (SUCCEEDED(m_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
		{
			m_dev->SetRenderTarget(0, bb);
			bb->Release();
		}
		m_dev->SetDepthStencilSurface(m_default_ds);
		m_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
	}

	// Render directly to backbuffer for RTX Remix compatibility
	// RTX Remix needs draws to go to the primary render target to raytrace them
	IDirect3DSurface9* backbuffer = nullptr;
	m_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
	if (backbuffer)
	{
		m_dev->SetRenderTarget(0, backbuffer);
		backbuffer->Release();
	}
	
	// Mark that we've rendered directly to backbuffer
	m_rendered_to_backbuffer = true;
	
	// Set viewport to window size
	D3DVIEWPORT9 vp = {};
	vp.Width = m_window_info.surface_width;
	vp.Height = m_window_info.surface_height;
	vp.MaxZ = 1.0f;
	m_dev->SetViewport(&vp);


	// Set texture if available, otherwise clear it
	if (config.tex)
	{
		GSTexture9* tex9 = static_cast<GSTexture9*>(config.tex);
		IDirect3DTexture9* d3dtex = tex9->GetTexture();
		if (d3dtex)
		{
			m_dev->SetTexture(0, d3dtex);
			m_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			m_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			m_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			m_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		}
		else
		{
			// Texture object exists but D3D texture is null - this shouldn't happen
			printf("DX9: config.tex exists but GetTexture() returned null! Type=%d\n", (int)tex9->GetType());
			m_dev->SetTexture(0, nullptr);
		}
	}
	else
	{
		m_dev->SetTexture(0, nullptr);
	}

	// Fixed function pipeline setup
	m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	
	// Disable all clipping - PS2 geometry may extend outside normal clip bounds
	m_dev->SetRenderState(D3DRS_CLIPPING, FALSE);
	
	// Disable user clip planes
	m_dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);

	// Disable D3D9 lighting - it overrides vertex colors and breaks texture display
	// RTX Remix will use its own lighting system
	m_dev->SetRenderState(D3DRS_LIGHTING, FALSE);

	// Set material with high specular for RTX Remix
	D3DMATERIAL9 mtrl = {};
	mtrl.Diffuse.r = 1.0f;
	mtrl.Diffuse.g = 1.0f;
	mtrl.Diffuse.b = 1.0f;
	mtrl.Diffuse.a = 1.0f;
	mtrl.Ambient.r = 0.2f;
	mtrl.Ambient.g = 0.2f;
	mtrl.Ambient.b = 0.2f;
	mtrl.Ambient.a = 1.0f;
	mtrl.Specular.r = 0.0f;
	mtrl.Specular.g = 0.0f;
	mtrl.Specular.b = 0.0f;
	mtrl.Specular.a = 0.0f;
	mtrl.Power = 0.0f;
	m_dev->SetMaterial(&mtrl);

	// Disable lights - let RTX Remix handle lighting
	m_dev->LightEnable(0, FALSE);
	m_dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(255, 255, 255));

	// Enable depth testing but respect game's depth write setting
	// Skyboxes typically have depth write disabled (zwe=0)
	m_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	m_dev->SetRenderState(D3DRS_ZWRITEENABLE, config.depth.zwe ? TRUE : FALSE);
	m_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	// Disable alpha blending for now - PS2 blending is complex
	m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

	// Enable alpha testing to handle cutout textures (foliage, fences, etc.)
	// Use low threshold to avoid culling semi-transparent hair
	m_dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	m_dev->SetRenderState(D3DRS_ALPHAREF, 1);  // Very low threshold - only discard fully transparent
	m_dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);

	// Texture stage state
	if (config.tex)
	{
		m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		m_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	}
	else
	{
		m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		m_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	}

	// Get render target size for coordinate transformation
	// Use window size since we're rendering directly to backbuffer
	const int rt_width = m_window_info.surface_width;
	const int rt_height = m_window_info.surface_height;

	// Use default depth buffer
	m_dev->SetDepthStencilSurface(m_default_ds);

	// Camera distance - geometry is placed at this Z depth
	const float camDist = 500.0f;

	// Set up transformation matrices for D3D9 and RTX Remix camera detection
	// World matrix - use captured MVP from VU1 if available
	D3DMATRIX world;
	if (m_vu1Capture.hasData)
	{
		// Use captured MVP matrix from VU1 (row-major, matches D3D)
		world._11 = m_vu1Capture.mvpMatrix[0];  world._12 = m_vu1Capture.mvpMatrix[1];
		world._13 = m_vu1Capture.mvpMatrix[2];  world._14 = m_vu1Capture.mvpMatrix[3];
		world._21 = m_vu1Capture.mvpMatrix[4];  world._22 = m_vu1Capture.mvpMatrix[5];
		world._23 = m_vu1Capture.mvpMatrix[6];  world._24 = m_vu1Capture.mvpMatrix[7];
		world._31 = m_vu1Capture.mvpMatrix[8];  world._32 = m_vu1Capture.mvpMatrix[9];
		world._33 = m_vu1Capture.mvpMatrix[10]; world._34 = m_vu1Capture.mvpMatrix[11];
		world._41 = m_vu1Capture.mvpMatrix[12]; world._42 = m_vu1Capture.mvpMatrix[13];
		world._43 = m_vu1Capture.mvpMatrix[14]; world._44 = m_vu1Capture.mvpMatrix[15];
	}
	else
	{
		// Identity if no VU1 data captured
		world._11 = 1; world._12 = 0; world._13 = 0; world._14 = 0;
		world._21 = 0; world._22 = 1; world._23 = 0; world._24 = 0;
		world._31 = 0; world._32 = 0; world._33 = 1; world._34 = 0;
		world._41 = 0; world._42 = 0; world._43 = 0; world._44 = 1;
	}
	m_dev->SetTransform(D3DTS_WORLD, &world);

	// Camera positioned to look at FFX geometry
	// Geometry is at X: -112 to +116, Y: -6 to +2, Z: -224 to -159
	// Place camera at Z=0, looking toward negative Z
	
	// Simple camera: at origin, looking down -Z axis (into the scene)
	// This is like D3DXMatrixLookAtLH(eye=(0,50,0), at=(0,0,-200), up=(0,1,0))
	// But simplified: just translate and rotate
	
	// View matrix: position camera to look at FFX geometry
	// FFX geometry is at approximately X: -20 to +6, Y: -10 to -3, Z: -170 to -145
	// Camera at (0, 0, 0) looking at (0, 0, -160)
	// For D3D LH: view matrix translates world so camera is at origin
	// Simple case: just translate geometry forward (positive Z)
	D3DMATRIX view = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 5, 160, 1  // Translate: cam looks at world origin offset by (0, -5, -160)
	};
	m_dev->SetTransform(D3DTS_VIEW, &view);

	// Perspective projection
	// FOV chosen so that at z=camDist, the view covers rt_width x rt_height
	float fovY = 2.0f * atanf((rt_height * 0.5f) / camDist);
	float aspect = (float)rt_width / (float)rt_height;
	float zn = 1.0f, zf = 10000.0f;
	float yScale = 1.0f / tanf(fovY * 0.5f);
	float xScale = yScale / aspect;
	D3DMATRIX proj = {
		xScale, 0, 0, 0,
		0, yScale, 0, 0,
		0, 0, zf / (zf - zn), 1,
		0, 0, -zn * zf / (zf - zn), 0
	};
	m_dev->SetTransform(D3DTS_PROJECTION, &proj);

	// Store camDist for vertex transform
	const float vertexZ = camDist;

	// ============================================
	// DEBUG: Render single VU1 mesh centered on screen
	// ============================================
	// Static storage for captured meshes (persists across frames)
	static std::vector<VU1InputSnapshot> capturedMeshes;
	static bool meshesCaptured = false;
	static u32 currentMeshIndex = 0;
	static u32 lastPrintedIndex = 0xFFFFFFFF;
	static std::chrono::steady_clock::time_point lastSwitchTime = std::chrono::steady_clock::now();
	const int secondsPerMesh = 5;
	
	// Capture meshes once when we have valid data
	// Simple copy - no splitting since GS uses triangle lists
	if (!meshesCaptured && !vu1Snapshots.empty())
	{
		for (const VU1InputSnapshot& snapshot : vu1Snapshots)
		{
			if (snapshot.vertexCount >= 3)
			{
				capturedMeshes.push_back(snapshot);
			}
		}
		
		if (!capturedMeshes.empty())
		{
			meshesCaptured = true;
			printf("Captured %zu separate meshes for visualization\n", capturedMeshes.size());
			
			// Debug: dump first snapshot's W values to find pattern
			if (!vu1Snapshots.empty())
			{
				const VU1InputSnapshot& snap = vu1Snapshots[0];
				printf("First snapshot: %u vertices\n", snap.vertexCount);
				printf("W values (first 50): ");
				for (u32 i = 0; i < snap.vertexCount && i < 50; i++)
				{
					printf("%.1f ", snap.vertices[i].w);
				}
				printf("\n");
				
				// Also check for position patterns
				printf("Position jumps (dist > 20):\n");
				for (u32 i = 1; i < snap.vertexCount && i < 100; i++)
				{
					const VU1RawVertex& prev = snap.vertices[i-1];
					const VU1RawVertex& curr = snap.vertices[i];
					float dx = curr.x - prev.x;
					float dy = curr.y - prev.y;
					float dz = curr.z - prev.z;
					float dist = sqrtf(dx*dx + dy*dy + dz*dz);
					if (dist > 20.0f)
					{
						printf("  [%u->%u] dist=%.1f\n", i-1, i, dist);
					}
				}
			}
		}
	}
	
	if (meshesCaptured && !capturedMeshes.empty())
	{
		// Compute scene bounds from all meshes
		static float sceneCenterX = 0, sceneCenterY = 0, sceneCenterZ = 0;
		static float sceneRadius = 100.0f;
		static bool boundsComputed = false;
		
		if (!boundsComputed)
		{
			float minX = 1e9f, maxX = -1e9f;
			float minY = 1e9f, maxY = -1e9f;
			float minZ = 1e9f, maxZ = -1e9f;
			
			for (const VU1InputSnapshot& mesh : capturedMeshes)
			{
				for (u32 i = 0; i < mesh.vertexCount; i++)
				{
					minX = std::min(minX, mesh.vertices[i].x);
					maxX = std::max(maxX, mesh.vertices[i].x);
					minY = std::min(minY, mesh.vertices[i].y);
					maxY = std::max(maxY, mesh.vertices[i].y);
					minZ = std::min(minZ, mesh.vertices[i].z);
					maxZ = std::max(maxZ, mesh.vertices[i].z);
				}
			}
			
			sceneCenterX = (minX + maxX) * 0.5f;
			sceneCenterY = (minY + maxY) * 0.5f;
			sceneCenterZ = (minZ + maxZ) * 0.5f;
			
			float dx = maxX - minX;
			float dy = maxY - minY;
			float dz = maxZ - minZ;
			sceneRadius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;
			
			printf("\n=== SCENE BOUNDS ===\n");
			printf("  X: [%.1f, %.1f]  Y: [%.1f, %.1f]  Z: [%.1f, %.1f]\n", minX, maxX, minY, maxY, minZ, maxZ);
			printf("  Center: (%.1f, %.1f, %.1f)  Radius: %.1f\n", sceneCenterX, sceneCenterY, sceneCenterZ, sceneRadius);
			printf("  Total meshes: %zu\n\n", capturedMeshes.size());
			
			boundsComputed = true;
		}
		
		// Set view matrix to look at scene center
		float camDist = (sceneRadius * 1.5f + 50.0f) / 20.0f;  // Divide by 20 to get even closer
		D3DMATRIX sceneView = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			-sceneCenterX, sceneCenterY, sceneCenterZ + camDist, 1
		};
		m_dev->SetTransform(D3DTS_VIEW, &sceneView);
		
		// Set identity world matrix
		D3DMATRIX vu1World = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
		m_dev->SetTransform(D3DTS_WORLD, &vu1World);
		
		// Setup rendering state
		m_dev->SetTexture(0, nullptr);
		m_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		m_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		m_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
		m_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		m_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		m_dev->SetFVF(GSVERTEXDX9_FVF);
		
		// Render ALL meshes
		for (size_t meshIdx = 0; meshIdx < capturedMeshes.size(); meshIdx++)
		{
			const VU1InputSnapshot& mesh = capturedMeshes[meshIdx];
			if (mesh.vertexCount < 3)
				continue;
			
			// Build vertices
			std::vector<GSVertexDX9> vu1Verts(mesh.vertexCount);
			for (u32 i = 0; i < mesh.vertexCount; i++)
			{
				const VU1RawVertex& srcV = mesh.vertices[i];
				GSVertexDX9& dstV = vu1Verts[i];
				
				dstV.x = srcV.x;
				dstV.y = -srcV.y;  // Flip Y for D3D
				dstV.z = -srcV.z;  // Flip Z (PS2 uses -Z forward)
				
				dstV.nx = 0.0f;
				dstV.ny = 0.0f;
				dstV.nz = -1.0f;
				
				// Matte gray for all meshes
				dstV.color = D3DCOLOR_ARGB(255, 128, 128, 128);
				
				dstV.u = srcV.hasUV ? srcV.s : 0.0f;
				dstV.v = srcV.hasUV ? srcV.t : 0.0f;
			}
			
			u32 triCount = mesh.vertexCount / 3;
			if (triCount > 0)
			{
				m_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount,
					vu1Verts.data(), sizeof(GSVertexDX9));
			}
		}
		
		// Restore matrices
		m_dev->SetTransform(D3DTS_VIEW, &view);
		m_dev->SetTransform(D3DTS_WORLD, &world);
	}

	// Convert GSVertex to D3D9 vertices
	const GSVertex* src = config.verts;
	const u32 nverts = config.nverts;

	std::vector<GSVertexDX9> transformed(nverts);

	// PS2 GS coordinates to D3D9 screen coordinates
	const float sx = config.cb_vs.vertex_scale.x;
	const float sy = config.cb_vs.vertex_scale.y;
	const float ox = config.cb_vs.vertex_offset.x;
	const float oy = config.cb_vs.vertex_offset.y;

	// First pass: compute positions
	for (u32 i = 0; i < nverts; i++)
	{
		const GSVertex& v = src[i];
		GSVertexDX9& d = transformed[i];

		// XYZ.X/Y are raw fixed-point values (4 fractional bits)
		float x = static_cast<float>(v.XYZ.X);
		float y = static_cast<float>(v.XYZ.Y);
		
		// Z is 32-bit - invert for D3D (smaller = closer)
		float z = 1.0f - static_cast<float>(static_cast<double>(v.XYZ.Z) / static_cast<double>(0xFFFFFFFFu));

		// Transform to NDC
		float ndc_x = x * sx - ox;
		float ndc_y = y * (-sy) + oy;

		// World-space coords centered at origin, pushed forward for perspective camera
		// Camera is at origin looking down +Z, geometry placed at camDist
		d.x = ndc_x * (rt_width * 0.5f);   // Center around origin
		d.y = ndc_y * (rt_height * 0.5f);  // Y already flipped by -sy above
		d.z = vertexZ + (z * 100.0f);       // Push forward + depth offset

		// Initialize normal to 0 (will be computed per-triangle)
		d.nx = 0.0f;
		d.ny = 0.0f;
		d.nz = -1.0f;  // Default facing camera

		// Color from RGBAQ (D3DCOLOR is ARGB)
		d.color = D3DCOLOR_ARGB(v.RGBAQ.A, v.RGBAQ.R, v.RGBAQ.G, v.RGBAQ.B);

		// Texture coordinates
		// PS2 uses fixed-point UVs with 4 fractional bits, so divide by 16
		// Then normalize to [0,1] range by dividing by texture dimensions
		if (config.tex)
		{
			const float tex_w = static_cast<float>(config.tex->GetWidth());
			const float tex_h = static_cast<float>(config.tex->GetHeight());
			d.u = (static_cast<float>(v.U) / 16.0f) / tex_w;
			d.v = (static_cast<float>(v.V) / 16.0f) / tex_h;
			
		}
		else
		{
			d.u = 0.0f;
			d.v = 0.0f;
		}
	}

	// Second pass: compute normals from triangles (for lighting)
	if (config.topology == GSHWDrawConfig::Topology::Triangle)
	{
		if (config.indices && config.nindices > 0)
		{
			// Indexed triangles
			for (u32 i = 0; i + 2 < config.nindices; i += 3)
			{
				u32 i0 = config.indices[i];
				u32 i1 = config.indices[i + 1];
				u32 i2 = config.indices[i + 2];
				
				GSVertexDX9& v0 = transformed[i0];
				GSVertexDX9& v1 = transformed[i1];
				GSVertexDX9& v2 = transformed[i2];

				// Compute triangle edges
				float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
				float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;

				// Cross product for face normal
				float nx = e1y * e2z - e1z * e2y;
				float ny = e1z * e2x - e1x * e2z;
				float nz = e1x * e2y - e1y * e2x;

				// Accumulate to vertex normals (will be normalized by D3D9)
				v0.nx += nx; v0.ny += ny; v0.nz += nz;
				v1.nx += nx; v1.ny += ny; v1.nz += nz;
				v2.nx += nx; v2.ny += ny; v2.nz += nz;
			}
		}
		else
		{
			// Non-indexed triangles
			for (u32 i = 0; i + 2 < nverts; i += 3)
			{
				GSVertexDX9& v0 = transformed[i];
				GSVertexDX9& v1 = transformed[i + 1];
				GSVertexDX9& v2 = transformed[i + 2];

				float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
				float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;

				float nx = e1y * e2z - e1z * e2y;
				float ny = e1z * e2x - e1x * e2z;
				float nz = e1x * e2y - e1y * e2x;

				v0.nx = nx; v0.ny = ny; v0.nz = nz;
				v1.nx = nx; v1.ny = ny; v1.nz = nz;
				v2.nx = nx; v2.ny = ny; v2.nz = nz;
			}
		}
	}

	// Draw the geometry
	m_dev->SetFVF(GSVERTEXDX9_FVF);

	D3DPRIMITIVETYPE d3d_topology = D3DPT_TRIANGLELIST;
	u32 prim_count = 0;
	switch (config.topology)
	{
		case GSHWDrawConfig::Topology::Point:
			d3d_topology = D3DPT_POINTLIST;
			prim_count = nverts;
			break;
		case GSHWDrawConfig::Topology::Line:
			d3d_topology = D3DPT_LINELIST;
			prim_count = nverts / 2;
			break;
		case GSHWDrawConfig::Topology::Triangle:
			d3d_topology = D3DPT_TRIANGLELIST;
			prim_count = nverts / 3;
			break;
	}

	// DISABLED: Original GS rendering - using VU1 capture instead
	// if (prim_count > 0)
	// {
	// 	if (config.indices && config.nindices > 0)
	// 	{
	// 		u32 index_prim_count = config.nindices / 3;
	// 		if (index_prim_count > 0)
	// 			m_dev->DrawIndexedPrimitiveUP(d3d_topology, 0, nverts, index_prim_count,
	// 				config.indices, D3DFMT_INDEX16, transformed.data(), sizeof(GSVertexDX9));
	// 	}
	// 	else
	// 	{
	// 		m_dev->DrawPrimitiveUP(d3d_topology, prim_count, transformed.data(), sizeof(GSVertexDX9));
	// 	}
	// }

	m_dev->SetTexture(0, nullptr);
}

void GSDevice9::ClearSamplerCache()
{
	// Clear shader caches
	for (auto& ps : m_ps_cache)
	{
		if (ps.second)
			ps.second->Release();
	}
	m_ps_cache.clear();

	for (auto& vs : m_vs_cache)
	{
		if (vs.second)
			vs.second->Release();
	}
	m_vs_cache.clear();
}

// Buffer management
void* GSDevice9::IAMapVertexBuffer(u32 stride, u32 count)
{
	u32 size = stride * count;
	if (size > VERTEX_BUFFER_SIZE)
		return nullptr;

	DWORD flags = (m_vb_pos + size > VERTEX_BUFFER_SIZE) ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
	if (flags == D3DLOCK_DISCARD)
		m_vb_pos = 0;

	void* data = nullptr;
	if (FAILED(m_vb->Lock(m_vb_pos, size, &data, flags)))
		return nullptr;

	m_vertex.start = m_vb_pos / stride;
	m_vb_pos += size;
	return data;
}

void GSDevice9::IAUnmapVertexBuffer(u32 stride, u32 count)
{
	m_vb->Unlock();
	m_vertex.count = count;
}

bool GSDevice9::IASetVertexBuffer(const void* vertex, u32 stride, u32 count)
{
	void* data = IAMapVertexBuffer(stride, count);
	if (!data)
		return false;

	std::memcpy(data, vertex, stride * count);
	IAUnmapVertexBuffer(stride, count);
	return true;
}

u16* GSDevice9::IAMapIndexBuffer(u32 count)
{
	if (count > INDEX_BUFFER_SIZE)
		return nullptr;

	DWORD flags = (m_ib_pos + count > INDEX_BUFFER_SIZE) ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
	if (flags == D3DLOCK_DISCARD)
		m_ib_pos = 0;

	void* data = nullptr;
	if (FAILED(m_ib->Lock(m_ib_pos * sizeof(u16), count * sizeof(u16), &data, flags)))
		return nullptr;

	m_index.start = m_ib_pos;
	m_ib_pos += count;
	return static_cast<u16*>(data);
}

void GSDevice9::IAUnmapIndexBuffer(u32 count)
{
	m_ib->Unlock();
	m_index.count = count;
}

bool GSDevice9::IASetIndexBuffer(const void* index, u32 count)
{
	u16* data = IAMapIndexBuffer(count);
	if (!data)
		return false;

	std::memcpy(data, index, count * sizeof(u16));
	IAUnmapIndexBuffer(count);
	return true;
}

void GSDevice9::IASetPrimitiveTopology(D3DPRIMITIVETYPE topology)
{
	// Topology set per draw call in D3D9
}

void GSDevice9::IASetVertexDeclaration(IDirect3DVertexDeclaration9* decl)
{
	if (m_state.vertex_decl != decl)
	{
		m_state.vertex_decl = decl;
		m_dev->SetVertexDeclaration(decl);
	}
}

void GSDevice9::DrawPrimitive()
{
	m_dev->SetStreamSource(0, m_vb, 0, sizeof(GSVertex));
	m_dev->DrawPrimitive(D3DPT_TRIANGLELIST, m_vertex.start, m_vertex.count / 3);
}

void GSDevice9::DrawIndexedPrimitive()
{
	m_dev->SetStreamSource(0, m_vb, 0, sizeof(GSVertex));
	m_dev->SetIndices(m_ib);
	m_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, m_vertex.start, 0, m_vertex.count, m_index.start, m_index.count / 3);
}

void GSDevice9::DrawIndexedPrimitive(int offset, int count)
{
	m_dev->SetStreamSource(0, m_vb, 0, sizeof(GSVertex));
	m_dev->SetIndices(m_ib);
	m_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, m_vertex.start, 0, m_vertex.count, m_index.start + offset, count / 3);
}

void GSDevice9::PSSetShaderResource(int i, GSTexture* sr)
{
	GSTexture9* tex9 = static_cast<GSTexture9*>(sr);
	IDirect3DTexture9* texture = tex9 ? tex9->GetTexture() : nullptr;
	
	if (m_state.textures[i] != texture)
	{
		m_state.textures[i] = texture;
		m_dev->SetTexture(i, texture);
	}
}

void GSDevice9::PSSetSamplerState(int i, PSSamplerSelector sel)
{
	// Setup sampler states
	m_dev->SetSamplerState(i, D3DSAMP_ADDRESSU, sel.tau ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
	m_dev->SetSamplerState(i, D3DSAMP_ADDRESSV, sel.tav ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
	m_dev->SetSamplerState(i, D3DSAMP_MINFILTER, sel.IsMinFilterLinear() ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	m_dev->SetSamplerState(i, D3DSAMP_MAGFILTER, sel.IsMagFilterLinear() ? D3DTEXF_LINEAR : D3DTEXF_POINT);
}

void GSDevice9::OMSetRenderTargets(GSTexture* rt, GSTexture* ds, const GSVector4i* scissor)
{
	// For RTX Remix: always render to backbuffer, ignore the passed RT
	// This ensures all geometry goes to the primary render target
	IDirect3DSurface9* backbuffer = nullptr;
	m_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
	if (backbuffer)
	{
		m_dev->SetRenderTarget(0, backbuffer);
		backbuffer->Release();
	}
	
	// Use default depth buffer
	m_dev->SetDepthStencilSurface(m_default_ds);
	
	// Disable scissor test - we're rendering to full backbuffer
	m_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
}

void GSDevice9::SetupVS(VSSelector sel, const GSHWDrawConfig::VSConstantBuffer* cb)
{
	// TODO: Implement vertex shader setup
}

void GSDevice9::SetupPS(const PSSelector& sel, const GSHWDrawConfig::PSConstantBuffer* cb, PSSamplerSelector ssel)
{
	// TODO: Implement pixel shader setup
}

void GSDevice9::SetupOM(OMDepthStencilSelector dssel, OMBlendSelector bsel, u8 afix)
{
	// TODO: Implement output merger setup
}

void GSDevice9::SetupDATE(GSTexture* rt, GSTexture* ds, SetDATM datm, const GSVector4i& bbox)
{
	// TODO: Implement DATE
}

// Window/Present functions
bool GSDevice9::UpdateWindow()
{
	return true;
}

void GSDevice9::ResizeWindow(u32 new_window_width, u32 new_window_height, float new_window_scale)
{
	// TODO: Handle device reset
}

bool GSDevice9::SupportsExclusiveFullscreen() const
{
	return false;
}

bool GSDevice9::HasSurface() const
{
	return m_window_info.type != WindowInfo::Type::Surfaceless;
}

void GSDevice9::DestroySurface()
{
	// TODO: Destroy swap chain
}

std::string GSDevice9::GetDriverInfo() const
{
	return m_name;
}

void GSDevice9::SetVSyncMode(GSVSyncMode mode, bool allow_present_throttle)
{
	m_vsync_mode = mode;
	m_allow_present_throttle = allow_present_throttle;
}

GSDevice::PresentResult GSDevice9::BeginPresent(bool frame_skip)
{
	if (frame_skip || !m_dev)
		return PresentResult::FrameSkipped;

	// Get and set backbuffer as render target
	IDirect3DSurface9* backbuffer = nullptr;
	if (FAILED(m_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)))
		return PresentResult::FrameSkipped;

	m_dev->SetRenderTarget(0, backbuffer);
	m_dev->SetDepthStencilSurface(m_default_ds);
	backbuffer->Release();

	// BeginScene is required for D3D9 rendering
	// Skip if RenderHW already started a scene
	if (!m_in_scene)
	{
		if (FAILED(m_dev->BeginScene()))
			return PresentResult::FrameSkipped;
		m_in_scene = true;
	}

	// Only clear if we rendered this frame - otherwise we'd flicker
	// (PS2 uses interlacing so RenderHW is only called every other frame)
	// Don't clear here - clear at start of RenderHW instead

	// Set viewport
	D3DVIEWPORT9 vp = {};
	vp.Width = m_window_info.surface_width;
	vp.Height = m_window_info.surface_height;
	vp.MaxZ = 1.0f;
	m_dev->SetViewport(&vp);

	return PresentResult::OK;
}

void GSDevice9::EndPresent()
{
	RenderImGui();

	if (m_dev)
	{
		if (m_in_scene)
		{
			m_dev->EndScene();
			m_in_scene = false;
		}
		
		// Only present if we actually rendered geometry this frame
		// PS2 uses interlacing so RenderHW is only called every other frame
		// Presenting on non-render frames causes flickering with RTX Remix
		if (m_rendered_to_backbuffer)
		{
			m_dev->Present(nullptr, nullptr, nullptr, nullptr);
		}
	}

	// Reset frame flags for next frame
	m_rendered_to_backbuffer = false;
}

void GSDevice9::RenderImGui()
{
	// End the ImGui frame - required even if we don't render
	ImGui::Render();

	// TODO: Implement actual D3D9 ImGui rendering
	// For now we just end the frame to prevent assertions
}

bool GSDevice9::SetGPUTimingEnabled(bool enabled)
{
	return false;
}

float GSDevice9::GetAndResetAccumulatedGPUTime()
{
	return 0.0f;
}

void GSDevice9::PushDebugGroup(const char* fmt, ...)
{
	// D3D9 doesn't support debug groups
}

void GSDevice9::PopDebugGroup()
{
}

void GSDevice9::InsertDebugMessage(DebugMessageCategory category, const char* fmt, ...)
{
}

void GSDevice9::EnableVU1Capture(bool enable)
{
	m_vu1Capture.enabled = enable;
	m_vu1Capture.hasData = false;

	// Also enable/disable the VU1 input capture for raw vertex data
	VU1InputCapture::GetInstance().SetEnabled(enable);

	if (!enable)
	{
		g_vu1LayoutConfig.Shutdown();
	}
}

void GSDevice9::CaptureVU1Memory(const u8* vu1Mem, u32 size)
{
	if (!m_vu1Capture.enabled || !vu1Mem || size == 0)
		return;

	// Lazy initialization of layout config when game serial becomes available
	if (g_vu1LayoutConfig.GetGameSerial().empty())
	{
		std::string gameSerial = VMManager::GetDiscSerial();
		if (!gameSerial.empty())
		{
			g_vu1LayoutConfig.Initialize(gameSerial);
		}
	}

	// Copy raw VU1 memory (16KB max)
	u32 copySize = std::min(size, (u32)sizeof(m_vu1Capture.rawMemory));
	std::memcpy(m_vu1Capture.rawMemory, vu1Mem, copySize);

	// Get the layout config for the current game
	const VU1MemoryLayout& layout = g_vu1LayoutConfig.GetLayout();

	// Extract MVP matrix from VU1 memory using configured offset
	const u32 mvpOffset = layout.mvpOffset;
	if (mvpOffset + 64 <= copySize)
	{
		const float* mvpData = reinterpret_cast<const float*>(vu1Mem + mvpOffset);
		
		if (layout.mvpColumnMajor)
		{
			// Transpose from column-major to row-major
			for (int row = 0; row < 4; row++)
			{
				for (int col = 0; col < 4; col++)
				{
					m_vu1Capture.mvpMatrix[row * 4 + col] = mvpData[col * 4 + row];
				}
			}
		}
		else
		{
			// Direct copy for row-major
			for (int i = 0; i < 16; i++)
			{
				m_vu1Capture.mvpMatrix[i] = mvpData[i];
			}
		}
	}

	// Extract bone matrices using configured offsets
	const u32 boneStartOffset = layout.boneStartOffset;
	const u32 boneStride = layout.boneStride;
	const u32 maxBones = std::min(layout.maxBones, (u32)64); // Cap at our array size
	const bool is4x4 = layout.bone4x4;
	const u32 boneSize = is4x4 ? 64 : 48; // 4x4 = 64 bytes, 4x3 = 48 bytes

	m_vu1Capture.boneCount = 0;
	for (u32 b = 0; b < maxBones; b++)
	{
		u32 boneOffset = boneStartOffset + (b * boneStride);
		if (boneOffset + boneSize > copySize)
			break;

		const float* boneData = reinterpret_cast<const float*>(vu1Mem + boneOffset);

		if (is4x4)
		{
			// Full 4x4 matrix
			if (layout.boneColumnMajor)
			{
				// Transpose from column-major to row-major
				for (int row = 0; row < 4; row++)
				{
					for (int col = 0; col < 4; col++)
					{
						m_vu1Capture.boneMatrices[b][row * 4 + col] = boneData[col * 4 + row];
					}
				}
			}
			else
			{
				for (int i = 0; i < 16; i++)
				{
					m_vu1Capture.boneMatrices[b][i] = boneData[i];
				}
			}
		}
		else
		{
			// 4x3 matrix - copy and extend to 4x4
			if (layout.boneColumnMajor)
			{
				// 3x4 column-major (transpose of 4x3 row-major)
				for (int row = 0; row < 4; row++)
				{
					for (int col = 0; col < 3; col++)
					{
						m_vu1Capture.boneMatrices[b][row * 4 + col] = boneData[col * 4 + row];
					}
				}
				// Fill last column
				m_vu1Capture.boneMatrices[b][3] = 0.0f;
				m_vu1Capture.boneMatrices[b][7] = 0.0f;
				m_vu1Capture.boneMatrices[b][11] = 0.0f;
				m_vu1Capture.boneMatrices[b][15] = 1.0f;
			}
			else
			{
				// Row 0
				m_vu1Capture.boneMatrices[b][0] = boneData[0];
				m_vu1Capture.boneMatrices[b][1] = boneData[1];
				m_vu1Capture.boneMatrices[b][2] = boneData[2];
				m_vu1Capture.boneMatrices[b][3] = boneData[3];
				// Row 1
				m_vu1Capture.boneMatrices[b][4] = boneData[4];
				m_vu1Capture.boneMatrices[b][5] = boneData[5];
				m_vu1Capture.boneMatrices[b][6] = boneData[6];
				m_vu1Capture.boneMatrices[b][7] = boneData[7];
				// Row 2
				m_vu1Capture.boneMatrices[b][8] = boneData[8];
				m_vu1Capture.boneMatrices[b][9] = boneData[9];
				m_vu1Capture.boneMatrices[b][10] = boneData[10];
				m_vu1Capture.boneMatrices[b][11] = boneData[11];
				// Row 3 (identity for 4x3 -> 4x4)
				m_vu1Capture.boneMatrices[b][12] = 0.0f;
				m_vu1Capture.boneMatrices[b][13] = 0.0f;
				m_vu1Capture.boneMatrices[b][14] = 0.0f;
				m_vu1Capture.boneMatrices[b][15] = 1.0f;
			}
		}

		m_vu1Capture.boneCount++;

		// Check if this looks like a valid bone matrix (has reasonable values)
		// Stop if we hit invalid data
		bool validMatrix = true;
		int checkCount = is4x4 ? 16 : 12;
		for (int i = 0; i < checkCount && validMatrix; i++)
		{
			float v = boneData[i];
			if (std::isnan(v) || std::isinf(v) || std::abs(v) > 10000.0f)
				validMatrix = false;
		}
		if (!validMatrix)
		{
			m_vu1Capture.boneCount--;
			break;
		}
	}

	m_vu1Capture.hasData = true;
}
