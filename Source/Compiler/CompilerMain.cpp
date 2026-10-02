#include "Compiler/CodeGenerator.h"
#include "Diagnostics/Diagnostics.h"
#include "Parser/HtmlParser.h"
#include "Validate/TreeValidator.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

std::string Quote(const std::string& text) {
    std::string quoted = "\"";
    for (char character : text) {
        if (character == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

std::string Argument(int argc, char** argv, const std::string& name) {
    for (int index = 1; index < argc - 1; ++index) {
        if (name == argv[index]) {
            return argv[index + 1];
        }
    }
    return "";
}

const char* kRuntimeSources[] = {
    "Dom/Document.cpp",
    "Catalog/ElementCatalog.cpp",
    "Layout/LayoutEngine.cpp",
    "Render/BitmapFont.cpp",
    "Render/ImageDecoder.cpp",
    "Render/Renderer.cpp",
    "Runtime/Interaction.cpp",
    "Runtime/HostLoop.cpp",
};

std::string ReadFile(const std::string& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open '" + path + "'";
        return "";
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteFile(const std::string& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output << text;
    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: RoboWebCompiler <input.html> -o <output.exe> --source-dir <Source>\n";
        return 1;
    }
    const std::string inputPath = argv[1];
    const std::string outputPath = Argument(argc, argv, "-o");
    const std::string sourceDir = Argument(argc, argv, "--source-dir");
    const std::string objectDir = Argument(argc, argv, "--object-dir");
    if (outputPath.empty() || sourceDir.empty()) {
        std::cerr << "RoboWebCompiler: error: missing -o or --source-dir\n";
        return 1;
    }
    const std::string fontObject = (objectDir.empty() ? outputPath : objectDir + "/InterFontData.o");
    std::string readError;
    const std::string source = ReadFile(inputPath, readError);
    if (!readError.empty()) {
        std::cerr << inputPath << ":1:1: error: " << readError << "\n";
        return 1;
    }
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::HtmlParser parser;
    RoboWeb::Document document = parser.Parse(source, inputPath, diagnostics);
    RoboWeb::ValidateTree(document, diagnostics);
    diagnostics.Print(std::cerr);
    if (diagnostics.HasErrors()) {
        std::cerr << "RoboWebCompiler: compilation failed with " << diagnostics.ErrorCount() << " error";
        if (diagnostics.ErrorCount() != 1) {
            std::cerr << "s";
        }
        std::cerr << "\n";
        return 1;
    }
    const std::string generated = RoboWeb::GenerateCpp(document);
    const std::string generatedPath = outputPath + ".cpp";
    if (!WriteFile(generatedPath, generated)) {
        std::cerr << "RoboWebCompiler: error: cannot write '" << generatedPath << "'\n";
        return 1;
    }
    const std::string fontSource = sourceDir + "/Render/InterFontData.cpp";
    bool fontFresh = false;
    struct stat fontObjectStat;
    struct stat fontSourceStat;
    if (stat(fontObject.c_str(), &fontObjectStat) == 0 && stat(fontSource.c_str(), &fontSourceStat) == 0) {
        fontFresh = fontObjectStat.st_mtime >= fontSourceStat.st_mtime;
    }
    if (!fontFresh) {
        const std::string fontCommand = "g++ -std=c++17 -O0 -Wall -Wextra -Wpedantic -I" + Quote(sourceDir) + " -c " +
                                        Quote(fontSource) + " -o " + Quote(fontObject);
        if (std::system(fontCommand.c_str()) != 0) {
            std::cerr << "RoboWebCompiler: error: native compile failed for Inter font data\n";
            return 1;
        }
    }
    std::string command = "g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I" + Quote(sourceDir) + " " + Quote(generatedPath);
    for (const char* sourceName : kRuntimeSources) {
        command += " " + Quote(sourceDir + "/" + sourceName);
    }
    command += " " + Quote(fontObject) + " -o " + Quote(outputPath);
    const int status = std::system(command.c_str());
    if (status != 0) {
        std::cerr << "RoboWebCompiler: error: native compile failed for '" << generatedPath << "'\n";
        return 1;
    }
    std::cerr << "RoboWebCompiler: wrote " << outputPath << "\n";
    return 0;
}
