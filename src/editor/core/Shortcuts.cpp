/**
 * Shortcuts.cpp
 * 
 * Implementation of keyboard shortcut management.
 */

#include "Shortcuts.h"
#include <imgui.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace Sanic::Editor {

std::string KeyBinding::toString() const {
    std::string result;
    
    if (modifiers & GLFW_MOD_CONTROL) result += "Ctrl+";
    if (modifiers & GLFW_MOD_ALT) result += "Alt+";
    if (modifiers & GLFW_MOD_SHIFT) result += "Shift+";
    if (modifiers & GLFW_MOD_SUPER) result += "Super+";
    
    // Key name
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        result += static_cast<char>('A' + (key - GLFW_KEY_A));
    } else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        result += static_cast<char>('0' + (key - GLFW_KEY_0));
    } else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        result += "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    } else {
        switch (key) {
            case GLFW_KEY_SPACE: result += "Space"; break;
            case GLFW_KEY_ESCAPE: result += "Escape"; break;
            case GLFW_KEY_ENTER: result += "Enter"; break;
            case GLFW_KEY_TAB: result += "Tab"; break;
            case GLFW_KEY_BACKSPACE: result += "Backspace"; break;
            case GLFW_KEY_DELETE: result += "Delete"; break;
            case GLFW_KEY_INSERT: result += "Insert"; break;
            case GLFW_KEY_HOME: result += "Home"; break;
            case GLFW_KEY_END: result += "End"; break;
            case GLFW_KEY_PAGE_UP: result += "PageUp"; break;
            case GLFW_KEY_PAGE_DOWN: result += "PageDown"; break;
            case GLFW_KEY_UP: result += "Up"; break;
            case GLFW_KEY_DOWN: result += "Down"; break;
            case GLFW_KEY_LEFT: result += "Left"; break;
            case GLFW_KEY_RIGHT: result += "Right"; break;
            default: result += "Key" + std::to_string(key); break;
        }
    }
    
    return result;
}

KeyBinding KeyBinding::fromString(const std::string& str) {
    KeyBinding binding;
    
    std::string remaining = str;
    
    // Parse modifiers
    while (true) {
        if (remaining.find("Ctrl+") == 0) {
            binding.modifiers |= GLFW_MOD_CONTROL;
            remaining = remaining.substr(5);
        } else if (remaining.find("Alt+") == 0) {
            binding.modifiers |= GLFW_MOD_ALT;
            remaining = remaining.substr(4);
        } else if (remaining.find("Shift+") == 0) {
            binding.modifiers |= GLFW_MOD_SHIFT;
            remaining = remaining.substr(6);
        } else if (remaining.find("Super+") == 0) {
            binding.modifiers |= GLFW_MOD_SUPER;
            remaining = remaining.substr(6);
        } else {
            break;
        }
    }
    
    // Parse key
    if (remaining.length() == 1) {
        char c = remaining[0];
        if (c >= 'A' && c <= 'Z') {
            binding.key = GLFW_KEY_A + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            binding.key = GLFW_KEY_A + (c - 'a');
        } else if (c >= '0' && c <= '9') {
            binding.key = GLFW_KEY_0 + (c - '0');
        }
    } else if (remaining.length() > 1 && remaining[0] == 'F') {
        int num = std::stoi(remaining.substr(1));
        if (num >= 1 && num <= 12) {
            binding.key = GLFW_KEY_F1 + num - 1;
        }
    } else if (remaining == "Space") binding.key = GLFW_KEY_SPACE;
    else if (remaining == "Escape") binding.key = GLFW_KEY_ESCAPE;
    else if (remaining == "Enter") binding.key = GLFW_KEY_ENTER;
    else if (remaining == "Tab") binding.key = GLFW_KEY_TAB;
    else if (remaining == "Backspace") binding.key = GLFW_KEY_BACKSPACE;
    else if (remaining == "Delete") binding.key = GLFW_KEY_DELETE;
    else if (remaining == "Insert") binding.key = GLFW_KEY_INSERT;
    else if (remaining == "Home") binding.key = GLFW_KEY_HOME;
    else if (remaining == "End") binding.key = GLFW_KEY_END;
    else if (remaining == "PageUp") binding.key = GLFW_KEY_PAGE_UP;
    else if (remaining == "PageDown") binding.key = GLFW_KEY_PAGE_DOWN;
    else if (remaining == "Up") binding.key = GLFW_KEY_UP;
    else if (remaining == "Down") binding.key = GLFW_KEY_DOWN;
    else if (remaining == "Left") binding.key = GLFW_KEY_LEFT;
    else if (remaining == "Right") binding.key = GLFW_KEY_RIGHT;
    
    return binding;
}

ShortcutManager::ShortcutManager() {
}

void ShortcutManager::registerShortcut(const std::string& name, KeyBinding binding,
                                        std::function<void()> action,
                                        const std::string& category) {
    Shortcut shortcut;
    shortcut.name = name;
    shortcut.binding = binding;
    shortcut.action = std::move(action);
    shortcut.category = category;
    
    shortcuts_[name] = std::move(shortcut);
    defaultBindings_[name] = binding;
}

void ShortcutManager::registerShortcut(const Shortcut& shortcut) {
    shortcuts_[shortcut.name] = shortcut;
    defaultBindings_[shortcut.name] = shortcut.binding;
}

void ShortcutManager::setBinding(const std::string& name, KeyBinding binding) {
    auto it = shortcuts_.find(name);
    if (it != shortcuts_.end()) {
        it->second.binding = binding;
    }
}

const Shortcut* ShortcutManager::getShortcut(const std::string& name) const {
    auto it = shortcuts_.find(name);
    return it != shortcuts_.end() ? &it->second : nullptr;
}

std::vector<const Shortcut*> ShortcutManager::getShortcutsByCategory(const std::string& category) const {
    std::vector<const Shortcut*> result;
    for (const auto& [name, shortcut] : shortcuts_) {
        if (shortcut.category == category) {
            result.push_back(&shortcut);
        }
    }
    return result;
}

std::vector<std::string> ShortcutManager::getCategories() const {
    std::vector<std::string> categories;
    for (const auto& [name, shortcut] : shortcuts_) {
        if (std::find(categories.begin(), categories.end(), shortcut.category) == categories.end()) {
            categories.push_back(shortcut.category);
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

bool ShortcutManager::triggerShortcut(const std::string& name) {
    auto it = shortcuts_.find(name);
    if (it != shortcuts_.end() && it->second.action) {
        it->second.action();
        return true;
    }
    return false;
}

void ShortcutManager::update() {
    if (!keyJustPressed_) return;
    
    keyJustPressed_ = false;
    
    // Find matching shortcut
    for (const auto& [name, shortcut] : shortcuts_) {
        if (shortcut.binding.key == currentKey_ && 
            shortcut.binding.modifiers == currentMods_) {
            if (shortcut.action) {
                shortcut.action();
            }
            break;
        }
    }
}

void ShortcutManager::keyPressed(int key, int mods) {
    currentKey_ = key;
    currentMods_ = mods;
    keyJustPressed_ = true;
}

void ShortcutManager::keyReleased(int key) {
    if (currentKey_ == key) {
        currentKey_ = 0;
        currentMods_ = 0;
    }
}

bool ShortcutManager::hasConflict(const KeyBinding& binding, const std::string& excludeName) const {
    for (const auto& [name, shortcut] : shortcuts_) {
        if (name == excludeName) continue;
        if (shortcut.binding == binding) return true;
    }
    return false;
}

void ShortcutManager::saveBindings(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;
    
    for (const auto& [name, shortcut] : shortcuts_) {
        file << name << "=" << shortcut.binding.toString() << "\n";
    }
}

void ShortcutManager::loadBindings(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string name = line.substr(0, pos);
            std::string binding = line.substr(pos + 1);
            setBinding(name, KeyBinding::fromString(binding));
        }
    }
}

void ShortcutManager::resetToDefaults() {
    for (const auto& [name, binding] : defaultBindings_) {
        setBinding(name, binding);
    }
}

} // namespace Sanic::Editor
