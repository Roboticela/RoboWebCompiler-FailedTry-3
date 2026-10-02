#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace RoboWeb {

enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string path;
    int line = 1;
    int column = 1;
    std::string message;
};

class Diagnostics {
public:
    void Report(Severity severity, const std::string& path, int line, int column, const std::string& message);
    void Error(const std::string& path, int line, int column, const std::string& message);
    void Warning(const std::string& path, int line, int column, const std::string& message);
    void Print(std::ostream& output) const;
    int ErrorCount() const;
    int WarningCount() const;
    bool HasErrors() const;
    const std::vector<Diagnostic>& Items() const;

private:
    std::vector<Diagnostic> items;
    int errorCount = 0;
    int warningCount = 0;
};

std::string SeverityName(Severity severity);

}
