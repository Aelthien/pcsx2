// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSTexture9.h"
#include "GS/GSPerfMon.h"

#include "common/Console.h"
#include "common/StringUtil.h"

GSTexture9::GSTexture9(IDirect3DTexture9* texture, const D3DSURFACE_DESC& desc, GSTexture::Type type, GSTexture::Format format)
	: m_texture(texture)
	, m_desc(desc)
{
	m_type = type;
	m_format = format;
	m_size.x = static_cast<int>(desc.Width);
	m_size.y = static_cast<int>(desc.Height);
	m_mipmap_levels = texture ? texture->GetLevelCount() : 1;

	// Get surface level 0 for operations that need it
	if (m_texture)
		m_texture->GetSurfaceLevel(0, &m_surface);
}

GSTexture9::GSTexture9(IDirect3DSurface9* surface, const D3DSURFACE_DESC& desc, GSTexture::Type type, GSTexture::Format format)
	: m_surface(surface)
	, m_desc(desc)
{
	m_type = type;
	m_format = format;
	m_size.x = static_cast<int>(desc.Width);
	m_size.y = static_cast<int>(desc.Height);
	m_mipmap_levels = 1;

	m_is_render_target = (desc.Usage & D3DUSAGE_RENDERTARGET) != 0;
	m_is_depth_stencil = (desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;

	// AddRef since we're storing it
	if (m_surface)
		m_surface->AddRef();
}

GSTexture9::~GSTexture9()
{
	if (m_surface)
		m_surface->Release();
	if (m_texture)
		m_texture->Release();
}

D3DFORMAT GSTexture9::GetD3D9Format(Format format)
{
	switch (format)
	{
		case Format::Color:
			return D3DFMT_A8R8G8B8;
		case Format::ColorHQ:
			return D3DFMT_A2R10G10B10;
		case Format::ColorHDR:
			return D3DFMT_A16B16G16R16F;
		case Format::DepthStencil:
			return D3DFMT_D24S8;
		case Format::UNorm8:
			return D3DFMT_A8R8G8B8;  // D3D9 doesn't have single channel, use ARGB
		case Format::UInt16:
			return D3DFMT_L16;
		case Format::UInt32:
			return D3DFMT_A8R8G8B8;  // Fallback
		case Format::PrimID:
			return D3DFMT_R32F;  // Use float for prim ID
		case Format::BC1:
			return D3DFMT_DXT1;
		case Format::BC2:
			return D3DFMT_DXT3;
		case Format::BC3:
			return D3DFMT_DXT5;
		case Format::BC7:
			return D3DFMT_A8R8G8B8;  // BC7 not supported in D3D9, fallback
		case Format::ColorClip:
			return D3DFMT_A8R8G8B8;
		default:
			return D3DFMT_A8R8G8B8;
	}
}

u32 GSTexture9::GetMemUsage(Format format, int width, int height)
{
	switch (format)
	{
		case Format::Color:
		case Format::ColorHQ:
		case Format::UNorm8:
		case Format::UInt32:
		case Format::ColorClip:
			return width * height * 4;
		case Format::ColorHDR:
			return width * height * 8;
		case Format::DepthStencil:
			return width * height * 4;
		case Format::UInt16:
			return width * height * 2;
		case Format::PrimID:
			return width * height * 4;
		case Format::BC1:
			return ((width + 3) / 4) * ((height + 3) / 4) * 8;
		case Format::BC2:
		case Format::BC3:
		case Format::BC7:
			return ((width + 3) / 4) * ((height + 3) / 4) * 16;
		default:
			return width * height * 4;
	}
}

void* GSTexture9::GetNativeHandle() const
{
	return m_texture ? static_cast<void*>(m_texture) : static_cast<void*>(m_surface);
}

bool GSTexture9::Update(const GSVector4i& r, const void* data, int pitch, int layer)
{
	if (!m_texture)
		return false;

	RECT rect;
	rect.left = r.x;
	rect.top = r.y;
	rect.right = r.z;
	rect.bottom = r.w;

	D3DLOCKED_RECT lr;
	HRESULT hr = m_texture->LockRect(layer, &lr, &rect, 0);
	if (FAILED(hr))
	{
		Console.Error("GSTexture9: Failed to lock texture for update (0x%08X)", hr);
		return false;
	}

	const int copy_width = r.z - r.x;
	const int copy_height = r.w - r.y;
	const u8* src = static_cast<const u8*>(data);
	u8* dst = static_cast<u8*>(lr.pBits);

	// Determine bytes per pixel based on format
	int bpp = 4;
	switch (m_format)
	{
		case Format::UInt16:
			bpp = 2;
			break;
		case Format::ColorHDR:
			bpp = 8;
			break;
		default:
			bpp = 4;
			break;
	}

	const int copy_pitch = copy_width * bpp;
	for (int y = 0; y < copy_height; y++)
	{
		if (bpp == 4 && m_format == Format::Color)
		{
			// Swizzle RGBA to BGRA for D3D9
			const u32* src32 = reinterpret_cast<const u32*>(src);
			u32* dst32 = reinterpret_cast<u32*>(dst);
			for (int x = 0; x < copy_width; x++)
			{
				u32 pixel = src32[x];
				// RGBA -> BGRA: swap R and B
				u32 r = (pixel >> 0) & 0xFF;
				u32 g = (pixel >> 8) & 0xFF;
				u32 b = (pixel >> 16) & 0xFF;
				u32 a = (pixel >> 24) & 0xFF;
				dst32[x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		}
		else
		{
			std::memcpy(dst, src, copy_pitch);
		}
		src += pitch;
		dst += lr.Pitch;
	}

	m_texture->UnlockRect(layer);
	return true;
}

bool GSTexture9::Map(GSMap& m, const GSVector4i* r, int layer)
{
	if (!m_texture && !m_surface)
		return false;

	RECT rect;
	RECT* pRect = nullptr;
	if (r)
	{
		rect.left = r->x;
		rect.top = r->y;
		rect.right = r->z;
		rect.bottom = r->w;
		pRect = &rect;
	}

	HRESULT hr;
	if (m_texture)
	{
		hr = m_texture->LockRect(layer, &m_locked, pRect, 0);
	}
	else
	{
		hr = m_surface->LockRect(&m_locked, pRect, 0);
	}

	if (FAILED(hr))
	{
		Console.Error("GSTexture9: Failed to lock texture/surface (0x%08X)", hr);
		return false;
	}

	m.bits = static_cast<u8*>(m_locked.pBits);
	m.pitch = m_locked.Pitch;
	return true;
}

void GSTexture9::Unmap()
{
	if (m_texture)
		m_texture->UnlockRect(0);
	else if (m_surface)
		m_surface->UnlockRect();

	m_locked = {};
}

void GSTexture9::GenerateMipmap()
{
	// D3D9 doesn't have automatic mipmap generation like D3D11
	// Would need to use D3DXFilterTexture or manual generation
	// For now, this is a no-op - mipmaps would need to be generated at creation time
	// with D3DUSAGE_AUTOGENMIPMAP flag
}

#ifdef PCSX2_DEVBUILD
void GSTexture9::SetDebugName(std::string_view name)
{
	// D3D9 doesn't have built-in debug naming like D3D11
	// Could use PIX markers or D3DPERF_SetMarker but those are global
}
#endif

// GSDownloadTexture9 implementation

GSDownloadTexture9::GSDownloadTexture9(IDirect3DSurface9* surface, u32 width, u32 height, GSTexture::Format format)
	: GSDownloadTexture(width, height, format)
	, m_surface(surface)
{
}

GSDownloadTexture9::~GSDownloadTexture9()
{
	if (m_surface)
		m_surface->Release();
}

std::unique_ptr<GSDownloadTexture9> GSDownloadTexture9::Create(IDirect3DDevice9* dev, u32 width, u32 height, GSTexture::Format format)
{
	D3DFORMAT d3dfmt = GSTexture9::GetD3D9Format(format);
	IDirect3DSurface9* surface = nullptr;

	// Create an offscreen plain surface for readback
	HRESULT hr = dev->CreateOffscreenPlainSurface(width, height, d3dfmt, D3DPOOL_SYSTEMMEM, &surface, nullptr);
	if (FAILED(hr))
	{
		Console.Error("GSDownloadTexture9: Failed to create offscreen surface (0x%08X)", hr);
		return nullptr;
	}

	return std::unique_ptr<GSDownloadTexture9>(new GSDownloadTexture9(surface, width, height, format));
}

void GSDownloadTexture9::CopyFromTexture(
	const GSVector4i& drc, GSTexture* stex, const GSVector4i& src, u32 src_level, bool use_transfer_pitch)
{
	GSTexture9* tex9 = static_cast<GSTexture9*>(stex);
	if (!tex9 || !m_surface)
		return;

	IDirect3DSurface9* src_surface = nullptr;
	IDirect3DTexture9* src_texture = tex9->GetTexture();

	if (src_texture)
	{
		src_texture->GetSurfaceLevel(src_level, &src_surface);
	}
	else
	{
		src_surface = tex9->GetSurface();
		if (src_surface)
			src_surface->AddRef();
	}

	if (!src_surface)
		return;

	// Get the device
	IDirect3DDevice9* dev = nullptr;
	src_surface->GetDevice(&dev);

	if (dev)
	{
		RECT srcRect = {src.x, src.y, src.z, src.w};
		RECT dstRect = {drc.x, drc.y, drc.z, drc.w};

		// Use GetRenderTargetData for render targets, otherwise StretchRect to temp then copy
		D3DSURFACE_DESC srcDesc;
		src_surface->GetDesc(&srcDesc);

		if (srcDesc.Pool == D3DPOOL_DEFAULT && (srcDesc.Usage & D3DUSAGE_RENDERTARGET))
		{
			dev->GetRenderTargetData(src_surface, m_surface);
		}
		// For non-RT textures, we'd need a more complex path

		dev->Release();
	}

	src_surface->Release();
}

bool GSDownloadTexture9::Map(const GSVector4i& rc)
{
	if (!m_surface)
		return false;

	RECT rect = {rc.x, rc.y, rc.z, rc.w};
	D3DLOCKED_RECT lr;

	HRESULT hr = m_surface->LockRect(&lr, &rect, D3DLOCK_READONLY);
	if (FAILED(hr))
	{
		Console.Error("GSDownloadTexture9: Failed to lock surface (0x%08X)", hr);
		return false;
	}

	m_map_pointer = static_cast<u8*>(lr.pBits);
	m_current_pitch = lr.Pitch;
	return true;
}

void GSDownloadTexture9::Unmap()
{
	if (m_surface)
		m_surface->UnlockRect();
	m_map_pointer = nullptr;
}

void GSDownloadTexture9::Flush()
{
	// D3D9 doesn't have explicit flush for surfaces
	// Operations are synchronous when we lock
}

#ifdef PCSX2_DEVBUILD
void GSDownloadTexture9::SetDebugName(std::string_view name)
{
	// No debug naming in D3D9
}
#endif
