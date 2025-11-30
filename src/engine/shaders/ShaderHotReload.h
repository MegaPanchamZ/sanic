/**
 * ShaderHotReload.h
 * 
 * File watcher for shader hot-reloading during development.
 * Monitors shader directories for changes and triggers recompilation.
 * 
 * Features:
 * - Background file watching thread
 * - Debouncing for rapid file changes
 * - Callback system for notification
 * - Manual trigger support
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <chrono>

namespace Sanic {

/**
 * Shader file change event
 */
struct ShaderFileEvent {
    enum class Type {
        Created,
        Modified,
        Deleted
    };
    
    Type type;
    std::filesystem::path path;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Shader hot-reload system
 */
class ShaderHotReload {
public:
    /**
     * Callback function type for shader reload notifications
     * @param path Path to the modified shader file
     */
    using ReloadCallback = std::function<void(const std::string& shaderPath)>;
    
    /**
     * Callback for batch notifications (all changes since last check)
     * @param events List of file change events
     */
    using BatchCallback = std::function<void(const std::vector<ShaderFileEvent>& events)>;
    
    ShaderHotReload();
    ~ShaderHotReload();
    
    // Non-copyable
    ShaderHotReload(const ShaderHotReload&) = delete;
    ShaderHotReload& operator=(const ShaderHotReload&) = delete;
    
    /**
     * Start watching directories for changes
     * @param watchPaths Directories to watch
     * @param recursive Whether to watch subdirectories
     * @return true if watching started successfully
     */
    bool start(const std::vector<std::filesystem::path>& watchPaths, bool recursive = true);
    
    /**
     * Stop watching for changes
     */
    void stop();
    
    /**
     * Check if watcher is running
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * Register callback for individual shader reloads
     * @param callback Function to call when a shader changes
     * @return ID for unregistering
     */
    uint32_t onReload(ReloadCallback callback);
    
    /**
     * Register callback for batch notifications
     * @param callback Function to call with all changes
     * @return ID for unregistering
     */
    uint32_t onBatchReload(BatchCallback callback);
    
    /**
     * Unregister a callback
     * @param id Callback ID returned from onReload/onBatchReload
     */
    void removeCallback(uint32_t id);
    
    /**
     * Manually trigger a check for changes
     * Useful when not using background thread
     */
    void checkForChanges();
    
    /**
     * Process pending change notifications
     * Should be called from main thread if using deferred callbacks
     */
    void processPendingChanges();
    
    /**
     * Get list of modified shaders since last check
     */
    std::vector<std::string> getModifiedShaders();
    
    /**
     * Add a file extension filter (default: .glsl, .vert, .frag, .comp, etc.)
     */
    void addExtensionFilter(const std::string& extension);
    
    /**
     * Clear extension filters (watch all files)
     */
    void clearExtensionFilters();
    
    /**
     * Set debounce delay (time to wait after change before notifying)
     * Helps with editors that save multiple times
     */
    void setDebounceDelay(std::chrono::milliseconds delay);
    
    /**
     * Force reload of a specific shader
     */
    void forceReload(const std::filesystem::path& path);
    
    /**
     * Get statistics
     */
    struct Stats {
        uint32_t filesWatched = 0;
        uint32_t directoriesWatched = 0;
        uint32_t reloadsTriggered = 0;
        std::chrono::system_clock::time_point lastReload;
    };
    Stats getStats() const;
    
private:
    void watchThread();
    void scanDirectory(const std::filesystem::path& dir, bool recursive);
    bool shouldWatch(const std::filesystem::path& path) const;
    void notifyCallbacks(const std::vector<ShaderFileEvent>& events);
    
    // Watch paths
    std::vector<std::filesystem::path> watchPaths_;
    bool recursive_ = true;
    
    // File timestamps
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps_;
    
    // Callbacks
    struct CallbackEntry {
        uint32_t id;
        std::variant<ReloadCallback, BatchCallback> callback;
    };
    std::vector<CallbackEntry> callbacks_;
    uint32_t nextCallbackId_ = 1;
    
    // Thread management
    std::thread watchThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    
    // Synchronization
    mutable std::mutex mutex_;
    std::mutex pendingMutex_;
    
    // Pending changes (for debouncing and deferred processing)
    std::vector<ShaderFileEvent> pendingEvents_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> pendingChanges_;
    std::chrono::milliseconds debounceDelay_{100};
    
    // Extension filters
    std::unordered_set<std::string> extensionFilters_;
    
    // Statistics
    mutable Stats stats_;
    
    // Poll interval
    static constexpr auto POLL_INTERVAL = std::chrono::milliseconds(100);
};

/**
 * Get the global shader hot-reload instance
 */
ShaderHotReload& GetShaderHotReload();

} // namespace Sanic
