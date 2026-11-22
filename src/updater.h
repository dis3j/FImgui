#pragma once
#include <string>

// Checks the latest GitHub release of FIMGUI_REPO in the background.
namespace updater
{
    struct Release
    {
        std::string version;   // "1.2.0"
        std::string tag;       // "v1.2.0"
        std::string name;
        std::string notes;     // release body (patch notes, Markdown)
        std::string url;       // release page
    };

    enum class State { Idle, Running, Done, Failed };

    void  CheckAsync();
    // Returns the state; when Done, *out holds the latest release; when Failed, *error says why.
    State Poll(Release* out, std::string* error);
    void  Reset();

    // -1 if a < b, 0 if equal, 1 if a > b ("1.10.0" > "1.9.3"; a leading 'v' is ignored).
    int   Compare(const std::string& a, const std::string& b);
    // Section "## <version>" of a CHANGELOG.md, without the heading line. Empty when missing.
    std::string NotesFor(const std::string& changelog, const std::string& version);
}
