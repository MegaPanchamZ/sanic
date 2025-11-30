/**
 * EditorLayout.h
 * 
 * Manages editor docking layout and serialization.
 */

#pragma once

#include <string>
#include <vector>
#include <imgui.h>

namespace Sanic::Editor {

// Layout preset
struct LayoutPreset {
    std::string name;
    std::string iniData;
};

class EditorLayout {
public:
    EditorLayout() = default;
    
    // Save/load current layout
    void saveLayout(const std::string& path);
    void loadLayout(const std::string& path);
    
    // Layout presets
    void savePreset(const std::string& name);
    void loadPreset(const std::string& name);
    void deletePreset(const std::string& name);
    std::vector<std::string> getPresetNames() const;
    
    // Default layout
    void applyDefaultLayout();
    
private:
    std::vector<LayoutPreset> presets_;
    std::string currentPreset_;
};

} // namespace Sanic::Editor
