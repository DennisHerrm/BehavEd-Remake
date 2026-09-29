// d3d11.h - Ersatzkopf, NUR zum Syntaxpruefen
//
// Warum es das gibt
// -----------------
// `gui/gpumap_win32.cpp` ist die einzige Datei im GPU-Weg, die sich ohne
// Windows nicht uebersetzen liess. Die Folge stand in shanks Bauprotokoll:
//
//     error C2065: "ecken": nichtdeklarierter Bezeichner
//
// Eine Variable im falschen Block - ein Fehler, den jeder Compiler in einer
// Sekunde findet, der aber erst auf einem fremden Rechner auffiel und dort
// eine ganze Runde gekostet hat.
//
// Diese Datei enthaelt gerade so viel von Direct3D 11, dass der Quelltext
// SYNTAKTISCH geprueft werden kann: Typen, Aufrufe, Namen, Sichtbarkeit.
//
// Was sie NICHT kann
// ------------------
// Sie prueft nicht, ob die Aufrufe richtig BENUTZT werden - ob ein Format
// passt, eine Bindung erlaubt ist, eine Reihenfolge stimmt. Ein Bild, das
// falsch aussieht, findet sie nicht. Sie findet Tippfehler, falsche
// Argumentzahlen und Variablen im falschen Block, und das ist genau die
// Klasse von Fehlern, die hier bisher am teuersten war.
//
// Sie wird NUR von tools/check_gui.sh benutzt und nie mitgebaut. Der echte
// Bau nimmt das Windows SDK.
#ifndef BHED_STUB_D3D11_H
#define BHED_STUB_D3D11_H

#include <cstdint>

using UINT = unsigned int;
using UINT64 = unsigned long long;
using BOOL = int;

// Fuer benenne() in gpumap_win32.cpp: derselbe Aufbau wie in guiddef.h,
// damit die Schreibweise der Klammern geprueft wird.
struct GUID {
    unsigned int Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
};
using HRESULT = long;
using FLOAT = float;

#define FALSE 0
#define TRUE 1
#define D3D11_FLOAT32_MAX 3.402823466e+38F

#ifndef __uuidof
#define __uuidof(T) static_cast<const void*>(nullptr)
#endif

#ifndef S_OK
#define S_OK 0
#endif
inline bool FAILED(HRESULT h) { return h < 0; }
inline bool SUCCEEDED(HRESULT h) { return h >= 0; }

struct IUnknownStub {
    void Release() {}
};

struct ID3D11DeviceChild : IUnknownStub {
    // Benennen fuer Debugschicht und RenderDoc - siehe benenne() in
    // gpumap_win32.cpp.
    HRESULT SetPrivateData(const GUID&, UINT, const void*) { return 0; }};
struct ID3D11Resource : ID3D11DeviceChild {};
struct ID3D11Texture2D : ID3D11Resource {};
struct ID3D11Buffer : ID3D11Resource {};
struct ID3D11VertexShader : ID3D11DeviceChild {};
struct ID3D11PixelShader : ID3D11DeviceChild {};
struct ID3D11InputLayout : ID3D11DeviceChild {};
struct ID3D11SamplerState : ID3D11DeviceChild {};
struct ID3D11BlendState : ID3D11DeviceChild {};
struct ID3D11DepthStencilState : ID3D11DeviceChild {};
struct ID3D11RasterizerState : ID3D11DeviceChild {};
struct ID3D11RenderTargetView : ID3D11DeviceChild {};
struct ID3D11ShaderResourceView : ID3D11DeviceChild {};
struct ID3D11DepthStencilView : ID3D11DeviceChild {};

enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R32_UINT,
    DXGI_FORMAT_R32G32_FLOAT,
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_D32_FLOAT,
};

enum D3D11_USAGE {
    D3D11_USAGE_DEFAULT = 0,
    D3D11_USAGE_IMMUTABLE,
    D3D11_USAGE_DYNAMIC,
};

enum D3D11_MAP { D3D11_MAP_WRITE_DISCARD = 4 };
enum D3D11_INPUT_CLASSIFICATION { D3D11_INPUT_PER_VERTEX_DATA = 0 };
enum D3D11_PRIMITIVE_TOPOLOGY { D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4 };
enum D3D11_FILTER { D3D11_FILTER_MIN_MAG_MIP_LINEAR = 0x15 };
enum D3D11_TEXTURE_ADDRESS_MODE {
    D3D11_TEXTURE_ADDRESS_WRAP = 1,
    D3D11_TEXTURE_ADDRESS_CLAMP = 3,
};

enum D3D11_BLEND {
    D3D11_BLEND_ZERO = 1,
    D3D11_BLEND_ONE,
    D3D11_BLEND_SRC_COLOR,
    D3D11_BLEND_INV_SRC_COLOR,
    D3D11_BLEND_SRC_ALPHA,
    D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA,
    D3D11_BLEND_INV_DEST_ALPHA,
    D3D11_BLEND_DEST_COLOR,
    D3D11_BLEND_INV_DEST_COLOR,
};
enum D3D11_BLEND_OP { D3D11_BLEND_OP_ADD = 1 };

constexpr UINT D3D11_BIND_VERTEX_BUFFER = 0x1;
constexpr UINT D3D11_BIND_INDEX_BUFFER = 0x2;
constexpr UINT D3D11_BIND_CONSTANT_BUFFER = 0x4;
constexpr UINT D3D11_BIND_SHADER_RESOURCE = 0x8;
constexpr UINT D3D11_BIND_RENDER_TARGET = 0x20;
constexpr UINT D3D11_BIND_DEPTH_STENCIL = 0x40;
constexpr UINT D3D11_CPU_ACCESS_WRITE = 0x10000;
constexpr UINT D3D11_CLEAR_DEPTH = 0x1;
constexpr UINT D3D11_COLOR_WRITE_ENABLE_ALL = 0xF;

struct DXGI_SAMPLE_DESC {
    UINT Count = 1;
    UINT Quality = 0;
};

struct D3D11_TEXTURE2D_DESC {
    UINT Width = 0;
    UINT Height = 0;
    UINT MipLevels = 0;
    UINT ArraySize = 0;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    DXGI_SAMPLE_DESC SampleDesc{};
    D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
    UINT BindFlags = 0;
    UINT CPUAccessFlags = 0;
    UINT MiscFlags = 0;
};

struct D3D11_BUFFER_DESC {
    UINT ByteWidth = 0;
    D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
    UINT BindFlags = 0;
    UINT CPUAccessFlags = 0;
    UINT MiscFlags = 0;
    UINT StructureByteStride = 0;
};

struct D3D11_SUBRESOURCE_DATA {
    const void* pSysMem = nullptr;
    UINT SysMemPitch = 0;
    UINT SysMemSlicePitch = 0;
};

struct D3D11_MAPPED_SUBRESOURCE {
    void* pData = nullptr;
    UINT RowPitch = 0;
    UINT DepthPitch = 0;
};

struct D3D11_INPUT_ELEMENT_DESC {
    const char* SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    D3D11_INPUT_CLASSIFICATION InputSlotClass;
    UINT InstanceDataStepRate;
};

enum D3D11_COMPARISON_FUNC { D3D11_COMPARISON_NEVER = 1,
                             D3D11_COMPARISON_LESS = 2,
                             D3D11_COMPARISON_EQUAL = 3,
                             D3D11_COMPARISON_LESS_EQUAL = 4,
                             D3D11_COMPARISON_ALWAYS = 8 };

struct D3D11_SAMPLER_DESC {
    D3D11_FILTER Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    D3D11_TEXTURE_ADDRESS_MODE AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    D3D11_TEXTURE_ADDRESS_MODE AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    D3D11_TEXTURE_ADDRESS_MODE AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    FLOAT MipLODBias = 0.0F;
    UINT MaxAnisotropy = 1;
    D3D11_COMPARISON_FUNC ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    FLOAT BorderColor[4]{};
    FLOAT MinLOD = 0.0F;
    FLOAT MaxLOD = 0.0F;
};

struct D3D11_RENDER_TARGET_BLEND_DESC {
    BOOL BlendEnable = FALSE;
    D3D11_BLEND SrcBlend = D3D11_BLEND_ONE;
    D3D11_BLEND DestBlend = D3D11_BLEND_ZERO;
    D3D11_BLEND_OP BlendOp = D3D11_BLEND_OP_ADD;
    D3D11_BLEND SrcBlendAlpha = D3D11_BLEND_ONE;
    D3D11_BLEND DestBlendAlpha = D3D11_BLEND_ZERO;
    D3D11_BLEND_OP BlendOpAlpha = D3D11_BLEND_OP_ADD;
    UINT RenderTargetWriteMask = 0;
};

struct D3D11_BLEND_DESC {
    BOOL AlphaToCoverageEnable = FALSE;
    BOOL IndependentBlendEnable = FALSE;
    D3D11_RENDER_TARGET_BLEND_DESC RenderTarget[8]{};
};

struct D3D11_VIEWPORT {
    FLOAT TopLeftX = 0.0F;
    FLOAT TopLeftY = 0.0F;
    FLOAT Width = 0.0F;
    FLOAT Height = 0.0F;
    FLOAT MinDepth = 0.0F;
    FLOAT MaxDepth = 1.0F;
};

// --- Abfragen ---------------------------------------------------------
//
// Fuer die Zeitmarken auf der Grafikkarte (gpumap_win32.cpp). Es reicht
// gerade so viel, dass Namen, Argumentzahlen und Typen geprueft werden.
enum D3D11_QUERY {
    D3D11_QUERY_EVENT = 0,
    D3D11_QUERY_OCCLUSION = 1,
    D3D11_QUERY_TIMESTAMP = 2,
    D3D11_QUERY_TIMESTAMP_DISJOINT = 3,
};

struct D3D11_QUERY_DESC {
    D3D11_QUERY Query;
    UINT MiscFlags;
};

struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT {
    UINT64 Frequency;
    int Disjoint;      // BOOL
};

struct ID3D11Asynchronous : IUnknownStub {};
struct ID3D11Query : ID3D11Asynchronous {};

#define D3D11_ASYNC_GETDATA_DONOTFLUSH 0x1U

// --- Tiefen- und Rasterzustand ----------------------------------------
enum D3D11_DEPTH_WRITE_MASK { D3D11_DEPTH_WRITE_MASK_ZERO = 0,
                              D3D11_DEPTH_WRITE_MASK_ALL = 1 };
enum D3D11_FILL_MODE { D3D11_FILL_WIREFRAME = 2, D3D11_FILL_SOLID = 3 };
enum D3D11_CULL_MODE { D3D11_CULL_NONE = 1, D3D11_CULL_FRONT = 2,
                       D3D11_CULL_BACK = 3 };

struct D3D11_DEPTH_STENCIL_DESC {
    int DepthEnable;
    D3D11_DEPTH_WRITE_MASK DepthWriteMask;
    D3D11_COMPARISON_FUNC DepthFunc;
    int StencilEnable;
};

struct D3D11_RASTERIZER_DESC {
    D3D11_FILL_MODE FillMode;
    D3D11_CULL_MODE CullMode;
    int FrontCounterClockwise;
    int DepthBias;
    float DepthBiasClamp;
    float SlopeScaledDepthBias;
    int DepthClipEnable;
    int ScissorEnable;
    int MultisampleEnable;
    int AntialiasedLineEnable;
};

// --- Debugschicht und Adapter -----------------------------------------
//
// Fuer den Bericht (gpumap_win32.cpp). Nur so viel, dass Namen,
// Argumentzahlen und Typen geprueft werden.
using SIZE_T = unsigned long;
using WCHAR = wchar_t;
using BOOL = int;

enum D3D11_MESSAGE_SEVERITY {
    D3D11_MESSAGE_SEVERITY_CORRUPTION = 0,
    D3D11_MESSAGE_SEVERITY_ERROR = 1,
    D3D11_MESSAGE_SEVERITY_WARNING = 2,
    D3D11_MESSAGE_SEVERITY_INFO = 3,
    D3D11_MESSAGE_SEVERITY_MESSAGE = 4,
};

struct D3D11_MESSAGE {
    int Category;
    D3D11_MESSAGE_SEVERITY Severity;
    int ID;
    const char* pDescription;
    SIZE_T DescriptionByteLength;
};

struct ID3D11InfoQueue : IUnknownStub {
    HRESULT SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY, BOOL) { return 0; }
    UINT64 GetNumStoredMessages() { return 0; }
    HRESULT GetMessage(UINT64, D3D11_MESSAGE*, SIZE_T*) { return 0; }
    void ClearStoredMessages() {}
};

struct DXGI_ADAPTER_DESC {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    unsigned long long DedicatedVideoMemory;
};

struct IDXGIAdapter : IUnknownStub {
    HRESULT GetDesc(DXGI_ADAPTER_DESC*) { return 0; }
};

struct IDXGIDevice : IUnknownStub {
    HRESULT GetAdapter(IDXGIAdapter**) { return 0; }
};

// Der wohlbekannte Bezeichner fuer den Namen eines Gegenstands.
inline const void* const WKPDID_D3DDebugObjectName = nullptr;

enum D3D_FEATURE_LEVEL { D3D_FEATURE_LEVEL_11_0 = 0xb000 };

struct ID3D11Device : IUnknownStub {
    HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC*,
                            const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**) {
        return 0;
    }
    HRESULT CreateBuffer(const D3D11_BUFFER_DESC*,
                         const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**) {
        return 0;
    }
    HRESULT CreateRenderTargetView(ID3D11Resource*, const void*,
                                   ID3D11RenderTargetView**) {
        return 0;
    }
    HRESULT CreateShaderResourceView(ID3D11Resource*, const void*,
                                     ID3D11ShaderResourceView**) {
        return 0;
    }
    HRESULT CreateDepthStencilView(ID3D11Resource*, const void*,
                                   ID3D11DepthStencilView**) {
        return 0;
    }
    HRESULT CreateVertexShader(const void*, std::size_t, void*,
                               ID3D11VertexShader**) {
        return 0;
    }
    HRESULT CreatePixelShader(const void*, std::size_t, void*,
                              ID3D11PixelShader**) {
        return 0;
    }
    HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC*, UINT,
                              const void*, std::size_t, ID3D11InputLayout**) {
        return 0;
    }
    HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC*,
                               ID3D11SamplerState**) {
        return 0;
    }
    HRESULT CreateBlendState(const D3D11_BLEND_DESC*, ID3D11BlendState**) {
        return 0;
    }
    HRESULT CreateQuery(const D3D11_QUERY_DESC*, ID3D11Query**) {
        return 0;
    }
    HRESULT QueryInterface(const void*, void**) { return 0; }
    D3D_FEATURE_LEVEL GetFeatureLevel() { return D3D_FEATURE_LEVEL_11_0; }
    HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC*,
                                    ID3D11DepthStencilState**) {
        return 0;
    }
    HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC*,
                                  ID3D11RasterizerState**) {
        return 0;
    }
};

struct ID3D11DeviceContext : IUnknownStub {
    void IASetVertexBuffers(UINT, UINT, ID3D11Buffer* const*, const UINT*,
                            const UINT*) {}
    void IASetIndexBuffer(ID3D11Buffer*, DXGI_FORMAT, UINT) {}
    void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY) {}
    void IASetInputLayout(ID3D11InputLayout*) {}
    void VSSetShader(ID3D11VertexShader*, void*, UINT) {}
    void PSSetShader(ID3D11PixelShader*, void*, UINT) {}
    void VSSetConstantBuffers(UINT, UINT, ID3D11Buffer* const*) {}
    void PSSetConstantBuffers(UINT, UINT, ID3D11Buffer* const*) {}
    void PSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView* const*) {}
    void PSSetSamplers(UINT, UINT, ID3D11SamplerState* const*) {}
    void OMSetRenderTargets(UINT, ID3D11RenderTargetView* const*,
                            ID3D11DepthStencilView*) {}
    void OMSetBlendState(ID3D11BlendState*, const FLOAT[4], UINT) {}
    void ClearRenderTargetView(ID3D11RenderTargetView*, const FLOAT[4]) {}
    void ClearDepthStencilView(ID3D11DepthStencilView*, UINT, FLOAT,
                               std::uint8_t) {}
    void RSSetViewports(UINT, const D3D11_VIEWPORT*) {}
    void DrawIndexed(UINT, UINT, int) {}
    void Draw(UINT, UINT) {}
    void CopyResource(ID3D11Resource*, ID3D11Resource*) {}
    HRESULT Map(ID3D11Resource*, UINT, D3D11_MAP, UINT,
                D3D11_MAPPED_SUBRESOURCE*) {
        return 0;
    }
    void Unmap(ID3D11Resource*, UINT) {}
    void OMSetDepthStencilState(ID3D11DepthStencilState*, UINT) {}
    void RSSetState(ID3D11RasterizerState*) {}
    void Begin(ID3D11Asynchronous*) {}
    void End(ID3D11Asynchronous*) {}
    HRESULT GetData(ID3D11Asynchronous*, void*, UINT, UINT) { return 0; }
};

#endif  // BHED_STUB_D3D11_H
