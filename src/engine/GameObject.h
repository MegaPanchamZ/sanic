#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <vulkan/vulkan.h>
#include "Mesh.h"
#include "Texture.h"

struct GameObject {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Texture> texture;
    glm::mat4 transform;
    VkDescriptorSet descriptorSet;
};
