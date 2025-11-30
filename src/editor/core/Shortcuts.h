/**
 * Shortcuts.h
 * 
 * Keyboard shortcut management for the editor.
 */

#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Sanic::Editor {

// Key combination
struct KeyBinding {
    int key = 0;
    int modifiers = 0;  // GLFW_MOD_CONTROL, GLFW_MOD_SHIFT, GLFW_MOD_ALT
    
    bool operator==(const KeyBinding& other) const {
        return key == other.key && modifiers == other.modifiers;
    }
    
    std::string toString() const;
    static KeyBinding fromString(const std::string& str);
};

// Shortcut definition
struct Shortcut {
    std::string name;
    std::string commandId;  // Optional, for command system integration
    KeyBinding binding;
    std::function<void()> action;
    std::string category = "General";
};

// Shortcut manager
class ShortcutManager {
public:
    ShortcutManager();
    
    // Register shortcuts
    void registerShortcut(const std::string& name, KeyBinding binding, 
                          std::function<void()> action,
                          const std::string& category = "General");
    
    void registerShortcut(const Shortcut& shortcut);
    
    // Update shortcut binding
    void setBinding(const std::string& name, KeyBinding binding);
    
    // Get shortcut info
    const Shortcut* getShortcut(const std::string& name) const;
    std::vector<const Shortcut*> getShortcutsByCategory(const std::string& category) const;
    std::vector<std::string> getCategories() const;
    
    // Trigger a shortcut by name
    bool triggerShortcut(const std::string& name);
    
    // Check current key state and trigger matching shortcuts
    void update();
    
    // Key state tracking
    void keyPressed(int key, int mods);
    void keyReleased(int key);
    
    // Conflict detection
    bool hasConflict(const KeyBinding& binding, const std::string& excludeName = "") const;
    
    // Save/load bindings
    void saveBindings(const std::string& path);
    void loadBindings(const std::string& path);
    void resetToDefaults();
    
private:
    std::unordered_map<std::string, Shortcut> shortcuts_;
    std::unordered_map<std::string, KeyBinding> defaultBindings_;
    
    // Current key state for update()
    int currentKey_ = 0;
    int currentMods_ = 0;
    bool keyJustPressed_ = false;
};

} // namespace Sanic::Editor
