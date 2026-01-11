// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSDevice9.h"
#include "GS/GSGL.h"
#include "GS/GSPerfMon.h"
#include "GS/GSUtil.h"
#include "GS/Renderers/Common/GSVertex.h"
#include "Host.h"

#include "common/Console.h"

#include "imgui.h"

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
	return true;
}

void GSDevice9::Destroy()
{
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
	m_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
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
	// Use pretransformed vertices with screen coordinates
	// The -0.5f offset is the D3D9 texel-to-pixel mapping correction
	const float left = dRect.x - 0.5f;
	const float top = dRect.y - 0.5f;
	const float right = dRect.z - 0.5f;
	const float bottom = dRect.w - 0.5f;

	// Use position + texcoord + color vertex format
	struct VertexPT1C {
		float x, y, z, rhw;
		DWORD color;
		float u, v;
	};

	VertexPT1C vertices[4] = {
		{left,  top,    0.0f, 1.0f, D3DCOLOR_XRGB(0,255,255), sRect.x, sRect.y},
		{right, top,    0.0f, 1.0f, D3DCOLOR_XRGB(0,255,255), sRect.z, sRect.y},
		{left,  bottom, 0.0f, 1.0f, D3DCOLOR_XRGB(0,255,255), sRect.x, sRect.w},
		{right, bottom, 0.0f, 1.0f, D3DCOLOR_XRGB(0,255,255), sRect.z, sRect.w},
	};

	m_dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	m_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VertexPT1C));
}

void GSDevice9::DoStretchRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	GSHWDrawConfig::ColorMaskSelector cms, ShaderConvert shader, bool linear)
{
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
	// Simple pass-through for now - just copy
	if (!sTex || !dTex || !m_dev)
		return;

	DoStretchRect(sTex, sRect, dTex, dRect, GSHWDrawConfig::ColorMaskSelector(), ShaderConvert::COPY, linear);
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

	// Set render targets
	OMSetRenderTargets(config.rt, config.ds, &config.scissor);

	// Clear render target if it's in cleared state (prevents ghosting)
	if (config.rt && config.rt->GetState() == GSTexture::State::Cleared)
	{
		u32 clear_color = config.rt->GetClearColor();
		m_dev->Clear(0, nullptr, D3DCLEAR_TARGET, clear_color, 1.0f, 0);
		config.rt->SetState(GSTexture::State::Dirty);
	}


	// Set texture if available
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
	}

	// Fixed function pipeline setup
	m_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	
	// Always use our default depth buffer for proper depth sorting
	if (m_default_ds)
		m_dev->SetDepthStencilSurface(m_default_ds);

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
	const int rt_width = config.rt ? config.rt->GetWidth() : m_window_info.surface_width;
	const int rt_height = config.rt ? config.rt->GetHeight() : m_window_info.surface_height;

	// Convert GSVertex to pretransformed D3D9 vertices
	const GSVertex* src = config.verts;
	const u32 nverts = config.nverts;

	std::vector<GSVertexDX9> transformed(nverts);

	// PS2 GS coordinates to D3D9 screen coordinates
	// From the Metal shader (tfx.metal line 176):
	//   out.p.xy = pos.xy * float2(scale.x, -scale.y) - float2(offset.x, -offset.y)
	// So: ndc_x = pos.x * scale.x - offset.x
	//     ndc_y = pos.y * (-scale.y) + offset.y
	const float sx = config.cb_vs.vertex_scale.x;
	const float sy = config.cb_vs.vertex_scale.y;
	const float ox = config.cb_vs.vertex_offset.x;
	const float oy = config.cb_vs.vertex_offset.y;

	for (u32 i = 0; i < nverts; i++)
	{
		const GSVertex& v = src[i];
		GSVertexDX9& d = transformed[i];

		// XYZ.X/Y are raw fixed-point values (4 fractional bits)
		// The scale already accounts for this (sx = 2.0 / (width << 4))
		// So we pass the raw integer value, not divided by 16
		float x = static_cast<float>(v.XYZ.X);
		float y = static_cast<float>(v.XYZ.Y);
		
		// Z is 32-bit - PS2 uses larger Z for closer objects (reversed depth)
		// Invert so larger PS2 Z becomes smaller D3D Z (closer to camera)
		float z = 1.0f - static_cast<float>(static_cast<double>(v.XYZ.Z) / static_cast<double>(0xFFFFFFFFu));

		// Transform to NDC (matching the real shader: pos * scale - offset)
		// Note: Y scale is negated in the shader
		float ndc_x = x * sx - ox;
		float ndc_y = y * (-sy) + oy;

		// NDC [-1,1] to screen [0, size]
		// D3D9 RHW needs the -0.5 texel offset
		// D3D9 has Y=0 at top, so flip: (1 - ndc_y) instead of (ndc_y + 1)
		d.x = (ndc_x + 1.0f) * 0.5f * rt_width - 0.5f;
		d.y = (1.0f - ndc_y) * 0.5f * rt_height - 0.5f;
		d.z = z;
		d.rhw = 1.0f;

		// Color from RGBAQ
		// D3DCOLOR is ARGB format (0xAARRGGBB)
		d.color = D3DCOLOR_ARGB(255, v.RGBAQ.R, v.RGBAQ.G, v.RGBAQ.B);

		// Texture coordinates
		if (config.tex)
		{
			d.u = static_cast<float>(v.U) / 16.0f / config.tex->GetWidth();
			d.v = static_cast<float>(v.V) / 16.0f / config.tex->GetHeight();
		}
		else
		{
			d.u = 0.0f;
			d.v = 0.0f;
		}
	}

	// Draw the actual game geometry
	m_dev->SetFVF(GSVERTEXDX9_FVF);

	D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
	u32 prim_count = 0;
	switch (config.topology)
	{
		case GSHWDrawConfig::Topology::Point:
			topology = D3DPT_POINTLIST;
			prim_count = nverts;
			break;
		case GSHWDrawConfig::Topology::Line:
			topology = D3DPT_LINELIST;
			prim_count = nverts / 2;
			break;
		case GSHWDrawConfig::Topology::Triangle:
			topology = D3DPT_TRIANGLELIST;
			prim_count = nverts / 3;
			break;
	}

	if (prim_count > 0)
	{
		if (config.indices && config.nindices > 0)
		{
			u32 index_prim_count = config.nindices / 3;
			if (index_prim_count > 0)
				m_dev->DrawIndexedPrimitiveUP(topology, 0, nverts, index_prim_count,
					config.indices, D3DFMT_INDEX16, transformed.data(), sizeof(GSVertexDX9));
		}
		else
		{
			m_dev->DrawPrimitiveUP(topology, prim_count, transformed.data(), sizeof(GSVertexDX9));
		}
	}

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
	GSTexture9* rt9 = static_cast<GSTexture9*>(rt);
	GSTexture9* ds9 = static_cast<GSTexture9*>(ds);

	IDirect3DSurface9* rt_surface = rt9 ? rt9->GetSurface() : nullptr;
	IDirect3DSurface9* ds_surface = ds9 ? ds9->GetSurface() : nullptr;

	if (m_state.rt != rt_surface)
	{
		m_state.rt = rt_surface;
		m_dev->SetRenderTarget(0, rt_surface);
	}

	if (m_state.ds != ds_surface)
	{
		m_state.ds = ds_surface;
		m_dev->SetDepthStencilSurface(ds_surface);
	}

	if (scissor)
	{
		RECT rc = {scissor->x, scissor->y, scissor->z, scissor->w};
		m_state.scissor = rc;
		m_dev->SetScissorRect(&rc);
		m_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
	}
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
	if (FAILED(m_dev->BeginScene()))
		return PresentResult::FrameSkipped;

	// Clear both color and depth buffer - dark orange background
	m_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255, 139, 69, 19), 1.0f, 0);

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
		m_dev->EndScene();
		m_dev->Present(nullptr, nullptr, nullptr, nullptr);
	}
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
