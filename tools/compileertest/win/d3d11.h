#pragma once
#include "windows.h"

typedef int DXGI_FORMAT;
#define DXGI_FORMAT_R8G8B8A8_UNORM 28

typedef int D3D11_USAGE;
#define D3D11_USAGE_DEFAULT 0
#define D3D11_BIND_SHADER_RESOURCE 0x8L

struct DXGI_SAMPLE_DESC { UINT Count; UINT Quality; };

struct D3D11_TEXTURE2D_DESC
{
    UINT Width, Height, MipLevels, ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags, CPUAccessFlags, MiscFlags;
};

struct D3D11_SUBRESOURCE_DATA { const void *pSysMem; UINT SysMemPitch, SysMemSlicePitch; };

struct D3D11_TEX2D_SRV { UINT MostDetailedMip, MipLevels; };
struct D3D11_SHADER_RESOURCE_VIEW_DESC
{
    DXGI_FORMAT Format;
    int ViewDimension;
    D3D11_TEX2D_SRV Texture2D;
};
#define D3D11_SRV_DIMENSION_TEXTURE2D 4

struct ID3D11Resource { virtual ~ID3D11Resource() {} virtual void Release() {} };
struct ID3D11Texture2D : ID3D11Resource {};
struct ID3D11ShaderResourceView : ID3D11Resource {};
struct ID3D11RenderTargetView : ID3D11Resource {};
struct ID3D11DepthStencilView;
struct ID3D11DeviceContext
{
    virtual ~ID3D11DeviceContext() {}
    virtual void OMGetRenderTargets( UINT, ID3D11RenderTargetView **, ID3D11DepthStencilView ** ) {}
    virtual void OMSetRenderTargets( UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView * ) {}
    virtual void Release() {}
};

struct ID3D11Device
{
    virtual ~ID3D11Device() {}
    virtual HRESULT CreateTexture2D( const D3D11_TEXTURE2D_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D ** ) { return 0; }
    virtual HRESULT CreateShaderResourceView( ID3D11Resource *, const D3D11_SHADER_RESOURCE_VIEW_DESC *, ID3D11ShaderResourceView ** ) { return 0; }
    virtual void GetImmediateContext( ID3D11DeviceContext ** ) {}
    virtual void Release() {}
};

struct IDXGISwapChain { virtual ~IDXGISwapChain() {} virtual void Release() {} };
