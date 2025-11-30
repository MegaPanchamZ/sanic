/**
 * EditorLayout.cpp
 * 
 * Implementation of layout management.
 */

#include "EditorLayout.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Sanic::Editor {

void EditorLayout::saveLayout(const std::string& path) {
    ImGui::SaveIniSettingsToDisk(path.c_str());
}

void EditorLayout::loadLayout(const std::string& path) {
    ImGui::LoadIniSettingsFromDisk(path.c_str());
}

void EditorLayout::savePreset(const std::string& name) {
    // Get current ImGui settings as string
    size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    
    // Find or create preset
    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&name](const LayoutPreset& p) { return p.name == name; });
    
    if (it != presets_.end()) {
        it->iniData = std::string(data, size);
    } else {
        presets_.push_back({name, std::string(data, size)});
    }
}

void EditorLayout::loadPreset(const std::string& name) {
    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&name](const LayoutPreset& p) { return p.name == name; });
    
    if (it != presets_.end()) {
        ImGui::LoadIniSettingsFromMemory(it->iniData.c_str(), it->iniData.size());
        currentPreset_ = name;
    }
}

void EditorLayout::deletePreset(const std::string& name) {
    presets_.erase(
        std::remove_if(presets_.begin(), presets_.end(),
            [&name](const LayoutPreset& p) { return p.name == name; }),
        presets_.end()
    );
    
    if (currentPreset_ == name) {
        currentPreset_.clear();
    }
}

std::vector<std::string> EditorLayout::getPresetNames() const {
    std::vector<std::string> names;
    names.reserve(presets_.size());
    for (const auto& preset : presets_) {
        names.push_back(preset.name);
    }
    return names;
}

void EditorLayout::applyDefaultLayout() {
    // Apply a sensible default layout
    // This would typically be done by building the dock layout programmatically
    // For now, we just let ImGui use its defaults
}

} // namespace Sanic::Editor
