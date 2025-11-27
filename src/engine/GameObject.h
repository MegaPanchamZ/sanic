#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <vulkan/vulkan.h>
#include "Mesh.h"
#include "Texture.h"

struct Material {
    std::shared_ptr<Texture> diffuse;
    std::shared_ptr<Texture> specular;
    std::shared_ptr<Texture> normal;
    float shininess = 32.0f;
};

struct GameObject {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    glm::mat4 transform;
    VkDescriptorSet descriptorSet;
};
