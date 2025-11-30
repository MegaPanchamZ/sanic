#include "RHI.h"
#include "vulkan/VulkanRHI.h"
#ifdef _WIN32
#include "d3d12/D3D12RHI.h"
#endif
#include <stdexcept>

namespace Sanic {

std::unique_ptr<IRHI> CreateRHI(RHIBackend backend) {
    switch (backend) {
        case RHIBackend::Vulkan:
            return std::make_unique<VulkanRHI>();
            
#ifdef _WIN32
        case RHIBackend::D3D12:
            return std::make_unique<D3D12RHI>();
#endif
            
        default:
            throw std::runtime_error("Unsupported RHI backend");
    }
}

bool IsRHIBackendAvailable(RHIBackend backend) {
    switch (backend) {
        case RHIBackend::Vulkan:
            // Vulkan is available if the SDK is present
            // Could add runtime check here
            return true;
            
        case RHIBackend::D3D12:
#ifdef _WIN32
            // D3D12 is available on Windows 10+
            return true;
#else
            return false;
#endif
            
        default:
            return false;
    }
}

} // namespace Sanic
