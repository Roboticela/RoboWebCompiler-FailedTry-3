#pragma once

#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"
#include "Render/Renderer.h"

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
    int width = -1;
    int height = -1;
    int contentStamp = 0;
    int geometryStamp = 0;
    int scrollY = 0;
    bool ready = false;
    bool dependsOnViewportHeight = true;
    LayoutResult layout;
    Framebuffer frame;
};

FrameStats& FrameStatistics();
void ResetFrameStatistics();
int ContentStamp(const Document& document);

FrameUpdate AdvanceFrame(ViewCache& cache, Document& document, int width, int height, FrameCause cause, Node* previousHover, bool settled);

}
