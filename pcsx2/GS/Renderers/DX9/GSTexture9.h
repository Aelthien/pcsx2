// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GS.h"
#include "GS/Renderers/Common/GSTexture.h"
#include "common/RedtapeWindows.h"

#include <d3d9.h>

class GSTexture9 final : public GSTexture
{
	IDirect3DTexture9* m_texture = nullptr;
	IDirect3DSurface9* m_surface = nullptr;  // For render targets/depth stencils
	D3DSURFACE_DESC m_desc = {};
	D3DLOCKED_RECT m_locked = {};
	bool m_is_render_target = false;
	bool m_is_depth_stencil = false;

public:
	GSTexture9(IDirect3DTexture9* texture, const D3DSURFACE_DESC& desc, GSTexture::Type type, GSTexture::Format format);
	GSTexture9(IDirect3DSurface9* surface, const D3DSURFACE_DESC& desc, GSTexture::Type type, GSTexture::Format format);
	~GSTexture9() override;

	static D3DFORMAT GetD3D9Format(Format format);
	static u32 GetMemUsage(Format format, int width, int height);

	void* GetNativeHandle() const override;

	bool Update(const GSVector4i& r, const void* data, int pitch, int layer = 0) override;
	bool Map(GSMap& m, const GSVector4i* r = nullptr, int layer = 0) override;
	void Unmap() override;
	void GenerateMipmap() override;

#ifdef PCSX2_DEVBUILD
	void SetDebugName(std::string_view name) override;
#endif

	IDirect3DTexture9* GetTexture() const { return m_texture; }
	IDirect3DSurface9* GetSurface() const { return m_surface; }

	// Conversion operators for convenience
	operator IDirect3DTexture9*() const { return m_texture; }
	operator IDirect3DSurface9*() const { return m_surface; }
};

class GSDownloadTexture9 final : public GSDownloadTexture
{
public:
	~GSDownloadTexture9() override;

	static std::unique_ptr<GSDownloadTexture9> Create(IDirect3DDevice9* dev, u32 width, u32 height, GSTexture::Format format);

	void CopyFromTexture(
		const GSVector4i& drc, GSTexture* stex, const GSVector4i& src, u32 src_level, bool use_transfer_pitch) override;

	bool Map(const GSVector4i& rc) override;
	void Unmap() override;

	void Flush() override;

#ifdef PCSX2_DEVBUILD
	void SetDebugName(std::string_view name) override;
#endif

private:
	GSDownloadTexture9(IDirect3DSurface9* surface, u32 width, u32 height, GSTexture::Format format);

	IDirect3DSurface9* m_surface = nullptr;
	IDirect3DDevice9* m_device = nullptr;
};
