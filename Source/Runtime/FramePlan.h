#pragma once

#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"
#include "Render/Renderer.h"

#include <vector>

namespace RoboWeb {

enum class FrameCause { Viewport, Content, Hover, Scroll, Caret };

struct FrameStats {
    int layoutCount = 0;
    int fullRasterCount = 0;
    int dirtyRasterCount = 0;
};

struct FrameUpdate {
    bool paint = false;
    bool full = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int scrollDelta = 0;
};

struct ViewCache {
    struct SavedLayout {
        int width = 0;
        int height = 0;
        int geometryStamp = 0;
        bool dependsOnViewportHeight = true;
        bool reflowsWithWidth = true;
        LayoutResult layout;
    };

    int width = -1;
    int height = -1;
    int contentStamp = 0;
    int geometryStamp = 0;
    int scrollY = 0;
    bool ready = false;
    bool dependsOnViewportHeight = true;
    bool reflowsWithWidth = true;
    LayoutResult layout;
    Framebuffer frame;
    std::vector<SavedLayout> saved;
};

struct BakedStorage {
    std::vector<std::string> heap;
    std::vector<BakedDraw> ops;
    std::vector<BakedBox> boxes;
    BakedFrame frame;
};

FrameStats& FrameStatistics();
void ResetFrameStatistics();
int ContentStamp(const Document& document);
int GeometryStamp(const Document& document);
bool DocumentDependsOnViewportHeight(const Document& document, const LayoutResult& layout);
bool PageReflowsWithWidth(const Document& document, const LayoutResult& layout);
void CaptureBakedFrame(const Document& document, const LayoutResult& layout, int geometryStamp, BakedStorage& storage);

FrameUpdate AdvanceFrame(ViewCache& cache, Document& document, int width, int height, FrameCause cause, Node* previousHover, bool settled);

}
