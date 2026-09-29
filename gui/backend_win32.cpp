// backend_win32.cpp - Direct3D 11 und OpenGL 3.3
//
// Beide in einer Datei, weil sie sich denselben Zustand und dieselbe
// Umschaltlogik teilen. Getrennt waeren es zwei Dateien mit je zehn Zeilen
// Eigenleben und einer gemeinsamen Kopfdatei fuer den Rest.

#include <cstdlib>

#include "bhed/diag.h"

#include "backend.h"
#include <string>
#include <cstdint>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include <windows.h>

#include <GL/gl.h>

// GL_CLAMP_TO_EDGE gibt es erst seit OpenGL 1.2; das mitgelieferte gl.h von
// Windows steht bei 1.1 stehen. Der Wert ist fest und aendert sich nicht.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// Direct3D 11 ist wahlweise.
//
// Wird es fest eingebunden, stehen d3d11.dll UND d3dcompiler_47.dll als
// harte Importe in der .exe - und Windows laedt das Programm dann gar nicht
// erst, wenn eine davon fehlt. Der Rueckfall auf OpenGL kaeme nie zum Zug,
// also genau in dem Fall nicht, fuer den er gedacht ist.
//
// Mit MSVC loest /DELAYLOAD das (siehe CMakeLists). Ohne verzoegertes Laden
// wird nur OpenGL gebaut. ImGui warnt uebrigens selbst davor, siehe den
// Kommentar in imgui_impl_dx11.cpp ueber D3DCOMPILER_DLL_A.
#ifdef BHED_WITH_D3D11
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#endif

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace bhed::render {
namespace {

Backend g_backend = Backend::Direct3D11;
bool g_ready = false;
HWND g_hwnd = nullptr;

// --- Direct3D 11 -------------------------------------------------------
#ifdef BHED_WITH_D3D11
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

void createRtv() {
    if (g_swap == nullptr) {
        return;
    }
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back != nullptr) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void releaseRtv() {
    if (g_rtv != nullptr) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool initD3D11(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    const D3D_FEATURE_LEVEL wanted[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};

    // Erst die Grafikkarte. Nur wenn die nichts liefert, WARP - das rechnet
    // auf der Hauptrechnereinheit und ist langsam, aber es laeuft ueberall.
    for (const D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        // --- Die Direct3D-Debugschicht ---------------------------------
        //
        // Sie prueft JEDEN Aufruf und meldet im Klartext, was nicht stimmt:
        // "Konstantenpuffer zu klein", "Eingabelayout passt nicht zum
        // Shader", "Textur gleichzeitig gebunden und beschrieben".
        //
        // Der GPU-Weg ist blind geschrieben worden - ohne Windows laesst er
        // sich hier nicht ausfuehren -, und ich habe dieses Werkzeug
        // dreissig Runden lang nicht eingeschaltet. Mehrere Umwege dieser
        // Sitzung haette es beim ersten Bild beendet.
        //
        // Nur mit BHED_D3D_DEBUG=1 in der Umgebung: die Schicht kostet
        // Leistung und ist auf Rechnern ohne "Grafiktools" gar nicht da.
        // Schlaegt das Anlegen damit fehl, wird es ohne sie wiederholt.
        // Immer AN versuchen, nicht nur auf Anforderung.
        //
        // shanks Frage: "warum muss ich ueberhaupt einen extra Knopf
        // aktivieren fuer das Debuggen? kann es das nicht immer automatisch
        // machen?" - Sie ist berechtigt. Ein Werkzeug, das man erst
        // einschalten muss, ist genau dann aus, wenn man es braucht.
        //
        // Die Schicht kostet Leistung, aber der Rasterer laeuft ohnehin bei
        // 240 fps und der GPU-Weg bei ueber 800. Gemessen wird mit
        // BHED_D3D_DEBUG=0, wenn es je darauf ankommt.
        //
        // Auf Rechnern ohne "Grafiktools" gibt es sie nicht - dann wird
        // stillschweigend ohne sie wiederholt.
        UINT flags = D3D11_CREATE_DEVICE_DEBUG;
        const char* dbg = std::getenv("BHED_D3D_DEBUG");
        if (dbg != nullptr && dbg[0] == '0') {
            flags = 0;
        }
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, driver, nullptr, flags, wanted,
            static_cast<UINT>(sizeof(wanted) / sizeof(wanted[0])),
            D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got, &g_context);
        if (FAILED(hr) && flags != 0) {
            // Ohne installierte Grafiktools gibt es die Schicht nicht.
            diag::detail("D3D-Debugschicht nicht verfuegbar - ohne sie");
            flags = 0;
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, driver, nullptr, 0, wanted,
                static_cast<UINT>(sizeof(wanted) / sizeof(wanted[0])),
                D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got,
                &g_context);
        } else if (SUCCEEDED(hr) && flags != 0) {
            diag::detail("D3D-Debugschicht AN - Meldungen im Debugfenster");
        }
        if (SUCCEEDED(hr)) {
            createRtv();
            return ImGui_ImplDX11_Init(g_device, g_context);
        }
    }
    return false;
}

void shutdownD3D11() {
    ImGui_ImplDX11_Shutdown();
    releaseRtv();
    if (g_swap != nullptr) { g_swap->Release(); g_swap = nullptr; }
    if (g_context != nullptr) { g_context->Release(); g_context = nullptr; }
    if (g_device != nullptr) { g_device->Release(); g_device = nullptr; }
}

#else
bool initD3D11(HWND) { return false; }
void shutdownD3D11() {}
#endif

// --- OpenGL 3.3 --------------------------------------------------------
HDC g_dc = nullptr;
HGLRC g_rc = nullptr;

// wglCreateContextAttribsARB gibt es nur ueber einen bereits bestehenden
// Kontext. Also: einfachen Kontext bauen, Funktion holen, richtigen Kontext
// bauen, einfachen wegwerfen.
using CreateCtxAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

bool initGL3(HWND hwnd) {
    g_dc = GetDC(hwnd);
    if (g_dc == nullptr) {
        return false;
    }
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    const int pf = ChoosePixelFormat(g_dc, &pfd);
    if (pf == 0 || SetPixelFormat(g_dc, pf, &pfd) == FALSE) {
        return false;
    }

    const HGLRC temp = wglCreateContext(g_dc);
    if (temp == nullptr || wglMakeCurrent(g_dc, temp) == FALSE) {
        return false;
    }

    auto createAttribs = reinterpret_cast<CreateCtxAttribs>(
        reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));
    if (createAttribs != nullptr) {
        const int attribs[] = {
            0x2091 /*MAJOR_VERSION*/, 3,
            0x2092 /*MINOR_VERSION*/, 3,
            0x9126 /*PROFILE_MASK*/,  0x00000001 /*CORE*/,
            0};
        if (HGLRC core = createAttribs(g_dc, nullptr, attribs)) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(temp);
            g_rc = core;
            wglMakeCurrent(g_dc, g_rc);
        }
    }
    if (g_rc == nullptr) {
        // Kein 3.3 zu bekommen: mit dem einfachen Kontext weitermachen.
        // ImGui kommt mit GLSL 130 auch auf OpenGL 3.0 zurecht.
        g_rc = temp;
    }
    return ImGui_ImplOpenGL3_Init(g_rc != temp ? "#version 330 core" : "#version 130");
}

void shutdownGL3() {
    ImGui_ImplOpenGL3_Shutdown();
    if (g_rc != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_rc);
        g_rc = nullptr;
    }
    if (g_dc != nullptr && g_hwnd != nullptr) {
        ReleaseDC(g_hwnd, g_dc);
        g_dc = nullptr;
    }
}

}  // namespace

namespace {

#ifdef BHED_WITH_D3D11
std::vector<ID3D11ShaderResourceView*> g_d3dTextures;
#endif
std::vector<unsigned int> g_glTextures;

}  // namespace

void* d3dDevice() {
#ifdef BHED_WITH_D3D11
    return (g_backend == Backend::Direct3D11) ? g_device : nullptr;
#else
    return nullptr;
#endif
}

void* d3dContext() {
#ifdef BHED_WITH_D3D11
    return (g_backend == Backend::Direct3D11) ? g_context : nullptr;
#else
    return nullptr;
#endif
}

void* createTexture(const unsigned char* rgba, int width, int height) {
    if (!g_ready) {
        return nullptr;
    }
#ifdef BHED_WITH_D3D11
    if (g_backend == Backend::Direct3D11) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = rgba;
        data.SysMemPitch = static_cast<UINT>(width) * 4U;

        ID3D11Texture2D* texture = nullptr;
        if (FAILED(g_device->CreateTexture2D(&desc, &data, &texture)) || texture == nullptr) {
            return nullptr;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = desc.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        const HRESULT hr = g_device->CreateShaderResourceView(texture, &view, &srv);
        texture->Release();
        if (FAILED(hr) || srv == nullptr) {
            return nullptr;
        }
        g_d3dTextures.push_back(srv);
        return srv;
    }
#endif
    GLuint id = 0;
    glGenTextures(1, &id);
    if (id == 0) {
        return nullptr;
    }
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    g_glTextures.push_back(id);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
}

void updateTexture(void* handle, const unsigned char* rgba, int width, int height) {
    if (handle == nullptr || !g_ready) {
        return;
    }
#ifdef BHED_WITH_D3D11
    if (g_backend == Backend::Direct3D11) {
        // Ueber die Sicht an die Textur kommen und den ganzen Inhalt
        // ersetzen. D3D11_USAGE_DEFAULT erlaubt UpdateSubresource.
        auto* srv = static_cast<ID3D11ShaderResourceView*>(handle);
        ID3D11Resource* res = nullptr;
        srv->GetResource(&res);
        if (res != nullptr) {
            g_context->UpdateSubresource(res, 0, nullptr, rgba,
                                         static_cast<UINT>(width) * 4U, 0);
            res->Release();
        }
        return;
    }
#endif
    const auto id = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(handle));
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}

void destroyTextures() {
#ifdef BHED_WITH_D3D11
    for (ID3D11ShaderResourceView* t : g_d3dTextures) {
        if (t != nullptr) { t->Release(); }
    }
    g_d3dTextures.clear();
#endif
    if (!g_glTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(g_glTextures.size()), g_glTextures.data());
        g_glTextures.clear();
    }
}

const char* backendName(Backend b) {
    return b == Backend::Direct3D11 ? "Direct3D 11" : "OpenGL 3.3";
}

bool init(Backend b, HWNDHandle hwnd) {
    g_hwnd = reinterpret_cast<HWND>(hwnd);
    g_backend = b;
    g_ready = (b == Backend::Direct3D11) ? initD3D11(g_hwnd) : initGL3(g_hwnd);
    if (!g_ready) {
        // Halb aufgebaute Zustaende wieder abraeumen, sonst scheitert der
        // zweite Versuch an Resten des ersten.
        if (b == Backend::Direct3D11) { shutdownD3D11(); } else { shutdownGL3(); }
    }
    return g_ready;
}

void shutdown() {
    if (!g_ready) {
        return;
    }
    destroyTextures();
    if (g_backend == Backend::Direct3D11) { shutdownD3D11(); } else { shutdownGL3(); }
    g_ready = false;
}

void newFrame() {
    if (!g_ready) {
        return;
    }
#ifdef BHED_WITH_D3D11
    if (g_backend == Backend::Direct3D11) {
        ImGui_ImplDX11_NewFrame();
        return;
    }
#endif
    ImGui_ImplOpenGL3_NewFrame();
}

std::string g_fotoPfad;   // leer = kein Foto angefordert

void fotoAnfordern(const std::string& pfad) { g_fotoPfad = pfad; }

#ifdef BHED_WITH_D3D11
// Den Hintergrundpuffer auslesen und als 32-Bit-BMP schreiben.
void fotoSchreiben() {
    const std::string pfad = g_fotoPfad;
    g_fotoPfad.clear();
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) || back == nullptr) { return; }
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* kopie = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &kopie)) && kopie != nullptr) {
        g_context->CopyResource(kopie, back);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(g_context->Map(kopie, 0, D3D11_MAP_READ, 0, &m))) {
            std::FILE* f = std::fopen(pfad.c_str(), "wb");
            if (f != nullptr) {
                const std::uint32_t w = d.Width, hgt = d.Height;
                const std::uint32_t bild = w * hgt * 4U;
                unsigned char kopf[54] = {'B', 'M'};
                auto u32 = [&](int at, std::uint32_t v) {
                    for (int k = 0; k < 4; ++k) { kopf[at + k] = static_cast<unsigned char>(v >> (8 * k)); }
                };
                u32(2, 54U + bild); u32(10, 54U); u32(14, 40U); u32(18, w);
                u32(22, static_cast<std::uint32_t>(-static_cast<std::int32_t>(hgt)));   // von oben
                kopf[26] = 1; kopf[28] = 32; u32(34, bild);
                std::fwrite(kopf, 1, sizeof(kopf), f);
                std::vector<unsigned char> zeile(w * 4U);
                for (std::uint32_t y = 0; y < hgt; ++y) {
                    const unsigned char* q = static_cast<const unsigned char*>(m.pData) + y * m.RowPitch;
                    for (std::uint32_t x = 0; x < w; ++x) {   // RGBA -> BGRA
                        zeile[x * 4U + 0U] = q[x * 4U + 2U];
                        zeile[x * 4U + 1U] = q[x * 4U + 1U];
                        zeile[x * 4U + 2U] = q[x * 4U + 0U];
                        zeile[x * 4U + 3U] = 255;
                    }
                    std::fwrite(zeile.data(), 1, zeile.size(), f);
                }
                std::fclose(f);
            }
            g_context->Unmap(kopie, 0);
        }
        kopie->Release();
    }
    back->Release();
}
#endif

void present(float r, float g, float bl) {
    if (!g_ready) {
        return;
    }
#ifdef BHED_WITH_D3D11
    if (g_backend == Backend::Direct3D11) {
        const float clear[4] = {r, g, bl, 1.0F};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (!g_fotoPfad.empty()) { fotoSchreiben(); }
        g_swap->Present(1, 0);
        return;
    }
#endif
    {
        const ImGuiIO& io = ImGui::GetIO();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x),
                   static_cast<int>(io.DisplaySize.y));
        glClearColor(r, g, bl, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_dc);
    }
}

void resize(int width, int height) {
    if (!g_ready || width <= 0 || height <= 0) {
        return;
    }
#ifdef BHED_WITH_D3D11
    if (g_backend == Backend::Direct3D11) {
        releaseRtv();
        g_swap->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                              DXGI_FORMAT_UNKNOWN, 0);
        createRtv();
    }
#else
    (void)width;
    (void)height;
#endif
    // Bei OpenGL genuegt der Aufruf von glViewport beim Zeichnen.
}

Backend current() { return g_backend; }
bool ready() { return g_ready; }

}  // namespace bhed::render
