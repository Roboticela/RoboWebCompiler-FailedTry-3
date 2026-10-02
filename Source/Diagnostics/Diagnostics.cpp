#include "Diagnostics/Diagnostics.h"

namespace RoboWeb {

std::string SeverityName(Severity severity) {
    if (severity == Severity::Error) {
        return "error";
    }
    if (severity == Severity::Warning) {
        return "warning";
    }
    return "note";
}

void Diagnostics::Report(
    Severity severity, const std::string& path, int line, int column, const std::string& message) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.path = path;
    diagnostic.line = line < 1 ? 1 : line;
    diagnostic.column = column < 1 ? 1 : column;
    diagnostic.message = message;
    items.push_back(diagnostic);
    if (severity == Severity::Error) {
        ++errorCount;
    } else if (severity == Severity::Warning) {
        ++warningCount;
    }
}

void Diagnostics::Error(const std::string& path, int line, int column, const std::string& message) {
    Report(Severity::Error, path, line, column, message);
}

void Diagnostics::Warning(const std::string& path, int line, int column, const std::string& message) {
    Report(Severity::Warning, path, line, column, message);
}

void Diagnostics::Print(std::ostream& output) const {
    for (const Diagnostic& diagnostic : items) {
        output << diagnostic.path << ":" << diagnostic.line << ":" << diagnostic.column << ": "
               << SeverityName(diagnostic.severity) << ": " << diagnostic.message << "\n";
    }
}

int Diagnostics::ErrorCount() const {
    return errorCount;
}

int Diagnostics::WarningCount() const {
    return warningCount;
}

bool Diagnostics::HasErrors() const {
    return errorCount > 0;
}

const std::vector<Diagnostic>& Diagnostics::Items() const {
    return items;
}

}
