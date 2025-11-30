#include "RHI.h"

#ifdef SANIC_ENABLE_VULKAN
#include "vulkan/VulkanRHI.h"
#endif

#ifdef SANIC_ENABLE_D3D12
#include "d3d12/D3D12RHI.h"
#endif

#include <stdexcept>

namespace Sanic {

std::unique_ptr<IRHI> CreateRHI(RHIBackend backend) {
    switch (backend) {
#ifdef SANIC_ENABLE_VULKAN
        case RHIBackend::Vulkan:
            return std::make_unique<VulkanRHI>();
#endif
            
#ifdef SANIC_ENABLE_D3D12
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
#ifdef SANIC_ENABLE_VULKAN
            return true;
#else
            return false;
#endif
            
        case RHIBackend::D3D12:
#ifdef SANIC_ENABLE_D3D12
            return true;
#else
            return false;
#endif
            
        default:
            return false;
    }
}

} // namespace Sanic
