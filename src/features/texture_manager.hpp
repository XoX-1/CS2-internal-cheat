#pragma once
#include <d3d11.h>
#include <string>

namespace TextureMgr {
    // Must be called once from the render thread with a valid DX11 device
    void Init(ID3D11Device* device);

    // Request a texture by URL. Returns the SRV if cached, or nullptr if still loading.
    // Triggers a background download + decode on first call.
    ID3D11ShaderResourceView* Get(const std::string& url);

    // Release all cached textures (call on shutdown)
    void Shutdown();
}
