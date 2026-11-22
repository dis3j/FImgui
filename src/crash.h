#pragma once
#include <string>

// Crash reports: an unhandled exception (access violation, uncaught C++ exception, abort, ...)
// writes crashlogs/crash_<time>.txt (+ .dmp) next to the exe and relaunches Fimgui with
// --crash-report <file>, which shows the report in a plain window (no message box, no sound).
namespace crash
{
    void Install();
    // What the app is doing right now ("Opening C:/x.fimgui"); printed in the report. Empty clears it.
    void SetContext(const std::string& what);
    std::string LogDir();
    // The --crash-report window; returns the process exit code.
    int ShowReport(const std::string& reportPath);
}
