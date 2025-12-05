#pragma once

#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;

namespace Sanic::Editor {

class ProjectHub {
public:
    struct ProjectInfo {
        std::string name;
        std::string path;
        std::string lastModified;
    };

    ProjectHub();
    ~ProjectHub();

    bool run(); // Returns true if a project was selected
    std::string getSelectedProjectPath() const { return selectedProjectPath_; }

private:
    void initialize();
    void shutdown();
    void draw();
    void loadRecentProjects();
    void saveRecentProjects();
    void createNewProject(const std::string& path, const std::string& name);

    GLFWwindow* window_ = nullptr;
    std::string selectedProjectPath_;
    std::vector<ProjectInfo> recentProjects_;
    bool shouldClose_ = false;
    
    // UI State
    char newProjectNameBuffer_[256] = "MyProject";
    char newProjectPathBuffer_[1024] = "";
    bool showNewProjectDialog_ = false;
};

} // namespace Sanic::Editor
