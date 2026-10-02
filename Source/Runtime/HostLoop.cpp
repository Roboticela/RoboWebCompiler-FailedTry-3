#include "Runtime/HostLoop.h"

#include "Core/Constants.h"
#include "Layout/LayoutEngine.h"
#include "Render/Renderer.h"
#include "Runtime/Interaction.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace RoboWeb {

namespace {

int ArgumentValue(int argc, char** argv, const std::string& name, int fallback) {
    for (int index = 1; index < argc - 1; ++index) {
        if (name == argv[index]) {
            return std::atoi(argv[index + 1]);
        }
    }
    return fallback;
}

std::string ArgumentText(int argc, char** argv, const std::string& name) {
    for (int index = 1; index < argc - 1; ++index) {
        if (name == argv[index]) {
            return argv[index + 1];
        }
    }
    return "";
}

bool HasFlag(int argc, char** argv, const std::string& name) {
    for (int index = 1; index < argc; ++index) {
        if (name == argv[index]) {
            return true;
        }
    }
    return false;
}

void RenderOnce(Document& document, int width, int height, const std::string& path) {
    LayoutResult layout = LayoutDocument(document, width, height);
    Framebuffer framebuffer = RenderDocument(layout, document.scrollY);
    if (!path.empty() && !WritePpm(framebuffer, path)) {
        std::cerr << "RoboWebCompiler: error: cannot write '" << path << "'\n";
    }
    std::cout << "TITLE " << document.title << "\n";
    std::cout << "FRAME " << framebuffer.width << " " << framebuffer.height << " " << path << "\n";
    if (!document.status.empty()) {
        std::cout << "STATUS " << document.status << "\n";
    }
    std::cout.flush();
}

}  // namespace

int RunDocument(Document& document, int argc, char** argv) {
    int width = ArgumentValue(argc, argv, "--width", DEFAULT_VIEWPORT_WIDTH_PX);
    int height = ArgumentValue(argc, argv, "--height", DEFAULT_VIEWPORT_HEIGHT_PX);
    const std::string output = ArgumentText(argc, argv, "--out");
    if (!HasFlag(argc, argv, "--interactive")) {
        RenderOnce(document, width, height, output);
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") {
            break;
        }
        if (line.rfind("viewport ", 0) == 0) {
            width = std::atoi(line.c_str() + 9);
            const std::size_t space = line.find(' ', 9);
            if (space != std::string::npos) {
                height = std::atoi(line.c_str() + space + 1);
            }
        } else if (line.rfind("click ", 0) == 0) {
            const int x = std::atoi(line.c_str() + 6);
            const std::size_t space = line.find(' ', 6);
            const int y = space == std::string::npos ? 0 : std::atoi(line.c_str() + space + 1);
            LayoutResult layout = LayoutDocument(document, width, height);
            DispatchClick(document, layout, x, y);
        } else if (line.rfind("key ", 0) == 0) {
            DispatchKey(document, line.substr(4));
        } else if (line.rfind("scroll ", 0) == 0) {
            LayoutResult layout = LayoutDocument(document, width, height);
            DispatchScroll(document, std::atoi(line.c_str() + 7), height, layout.contentHeight);
        } else if (line == "tick") {
            TickMarquee(document);
        } else if (line != "render" && !line.empty()) {
            std::cerr << "RoboWebCompiler: error: unknown host command '" << line << "'\n";
            return 1;
        }
        RenderOnce(document, width, height, output);
    }
    return 0;
}

int RunCompiledDocument(const StaticDocument& source, int argc, char** argv) {
    Document document;
    document.sourcePath = source.sourcePath != nullptr ? source.sourcePath : "";
    document.root = BuildFromStatic(source);
    CollectDocumentInfo(document);
    return RunDocument(document, argc, argv);
}

}
