/**
 * Commands.h
 * 
 * Editor command system for menu items and actions.
 */

#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Sanic::Editor {

class Editor;

// Command flags
enum class CommandFlags {
    None = 0,
    RequiresSelection = 1 << 0,
    RequiresWorld = 1 << 1,
    RequiresEditMode = 1 << 2,
    RequiresPlayMode = 1 << 3,
};

inline CommandFlags operator|(CommandFlags a, CommandFlags b) {
    return static_cast<CommandFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool operator&(CommandFlags a, CommandFlags b) {
    return (static_cast<int>(a) & static_cast<int>(b)) != 0;
}

// Command definition
struct Command {
    std::string id;
    std::string name;
    std::string category;
    std::string shortcut;
    std::function<void()> execute;
    std::function<bool()> canExecute;  // Optional
    CommandFlags flags = CommandFlags::None;
};

// Command manager
class CommandManager {
public:
    CommandManager(Editor* editor);
    
    // Register a command
    void registerCommand(const Command& command);
    void registerCommand(const std::string& id, const std::string& name,
                         std::function<void()> execute,
                         const std::string& category = "General",
                         CommandFlags flags = CommandFlags::None);
    
    // Execute a command by ID
    bool executeCommand(const std::string& id);
    
    // Check if command can be executed
    bool canExecuteCommand(const std::string& id) const;
    
    // Get command info
    const Command* getCommand(const std::string& id) const;
    std::vector<const Command*> getCommandsByCategory(const std::string& category) const;
    std::vector<std::string> getCategories() const;
    
    // Register built-in commands
    void registerBuiltInCommands();
    
private:
    Editor* editor_;
    std::unordered_map<std::string, Command> commands_;
};

} // namespace Sanic::Editor
