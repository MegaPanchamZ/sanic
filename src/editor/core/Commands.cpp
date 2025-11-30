/**
 * Commands.cpp
 * 
 * Implementation of command system.
 */

#include "Commands.h"
#include "../Editor.h"
#include <algorithm>

namespace Sanic::Editor {

CommandManager::CommandManager(Editor* editor)
    : editor_(editor) {
}

void CommandManager::registerCommand(const Command& command) {
    commands_[command.id] = command;
}

void CommandManager::registerCommand(const std::string& id, const std::string& name,
                                       std::function<void()> execute,
                                       const std::string& category,
                                       CommandFlags flags) {
    Command cmd;
    cmd.id = id;
    cmd.name = name;
    cmd.category = category;
    cmd.execute = std::move(execute);
    cmd.flags = flags;
    commands_[id] = std::move(cmd);
}

bool CommandManager::executeCommand(const std::string& id) {
    auto it = commands_.find(id);
    if (it == commands_.end()) return false;
    
    if (!canExecuteCommand(id)) return false;
    
    it->second.execute();
    return true;
}

bool CommandManager::canExecuteCommand(const std::string& id) const {
    auto it = commands_.find(id);
    if (it == commands_.end()) return false;
    
    const Command& cmd = it->second;
    
    // Check custom canExecute
    if (cmd.canExecute && !cmd.canExecute()) {
        return false;
    }
    
    // Check flags
    if (cmd.flags & CommandFlags::RequiresSelection) {
        if (!editor_->getSelection().hasSelection()) return false;
    }
    
    if (cmd.flags & CommandFlags::RequiresWorld) {
        if (!editor_->getWorld()) return false;
    }
    
    if (cmd.flags & CommandFlags::RequiresEditMode) {
        if (editor_->getMode() != EditorMode::Edit) return false;
    }
    
    if (cmd.flags & CommandFlags::RequiresPlayMode) {
        if (editor_->getMode() != EditorMode::Play) return false;
    }
    
    return true;
}

const Command* CommandManager::getCommand(const std::string& id) const {
    auto it = commands_.find(id);
    return it != commands_.end() ? &it->second : nullptr;
}

std::vector<const Command*> CommandManager::getCommandsByCategory(const std::string& category) const {
    std::vector<const Command*> result;
    for (const auto& [id, cmd] : commands_) {
        if (cmd.category == category) {
            result.push_back(&cmd);
        }
    }
    return result;
}

std::vector<std::string> CommandManager::getCategories() const {
    std::vector<std::string> categories;
    for (const auto& [id, cmd] : commands_) {
        if (std::find(categories.begin(), categories.end(), cmd.category) == categories.end()) {
            categories.push_back(cmd.category);
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

void CommandManager::registerBuiltInCommands() {
    // File commands
    registerCommand("file.new", "New Scene", [this]() { editor_->newScene(); }, "File");
    registerCommand("file.open", "Open Scene", [this]() { editor_->openScene(); }, "File");
    registerCommand("file.save", "Save Scene", [this]() { editor_->saveScene(); }, "File");
    registerCommand("file.saveAs", "Save Scene As", [this]() { editor_->saveSceneAs(); }, "File");
    
    // Edit commands
    registerCommand("edit.undo", "Undo", [this]() { editor_->getUndoSystem().undo(); }, "Edit");
    registerCommand("edit.redo", "Redo", [this]() { editor_->getUndoSystem().redo(); }, "Edit");
    
    registerCommand("edit.delete", "Delete", [this]() {
        auto& sel = editor_->getSelection();
        auto* world = editor_->getWorld();
        if (sel.hasSelection() && world) {
            for (Entity e : sel.getSelection()) {
                world->destroyEntity(e);
            }
            sel.clearSelection();
        }
    }, "Edit", CommandFlags::RequiresSelection | CommandFlags::RequiresWorld);
    
    registerCommand("edit.selectAll", "Select All", [this]() {
        auto* world = editor_->getWorld();
        if (world) {
            editor_->getSelection().selectAll(*world);
        }
    }, "Edit", CommandFlags::RequiresWorld);
    
    // Create commands
    registerCommand("create.empty", "Create Empty", [this]() {
        auto* world = editor_->getWorld();
        if (world) {
            Entity e = world->createEntity("Empty");
            world->addComponent<Transform>(e);
            editor_->getSelection().select(e);
        }
    }, "Create", CommandFlags::RequiresWorld);
    
    // Play commands
    registerCommand("play.play", "Play", [this]() { editor_->play(); }, "Play");
    registerCommand("play.pause", "Pause", [this]() { editor_->pause(); }, "Play");
    registerCommand("play.stop", "Stop", [this]() { editor_->stop(); }, "Play");
}

} // namespace Sanic::Editor
