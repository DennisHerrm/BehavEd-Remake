// d3dcompiler.h - Ersatzkopf, NUR zum Syntaxpruefen. Siehe d3d11.h.
#ifndef BHED_STUB_D3DCOMPILER_H
#define BHED_STUB_D3DCOMPILER_H

// Fahnen fuer D3DCompile. Nur die beiden, die behaved benutzt - siehe
// uebersetzFahnen() in gpumap_win32.cpp.
#define D3DCOMPILE_DEBUG             (1U << 0)
#define D3DCOMPILE_SKIP_OPTIMIZATION (1U << 2)

#include <cstddef>

#include "d3d11.h"

struct ID3DBlob : IUnknownStub {
    void* GetBufferPointer() { return nullptr; }
    std::size_t GetBufferSize() { return 0; }
};

inline HRESULT D3DCompile(const void*, std::size_t, const char*, const void*,
                          void*, const char*, const char*, UINT, UINT,
                          ID3DBlob**, ID3DBlob**) {
    return 0;
}

#endif  // BHED_STUB_D3DCOMPILER_H
