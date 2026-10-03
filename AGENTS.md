The only objective is that a page written in HTML, CSS, and the supported script subset runs at the same performance as the same screen written by hand in C++. Browser-identical pixels and behavior stay mandatory. A faster path that paints the wrong box, drops a default element style, or skips a test is a failed change.

C++ also repaints, resizes, and handles hover. Those events are required. The generated program does the same work a C++ UI does inside the event, and it does not replay the web pipeline.

- Fixed geometry, colors, text, and images are constants or static tables. Font and image bytes stay in the static arrays from code generation. Startup must not copy them into a `std::vector`.
- A resize runs arithmetic or `LayoutDocument` only because the viewport changed, then paints the new frame.
- A hover that changes color sets the hover node and repaints that control's rectangle from the cached draw records. It does not call `LayoutDocument`, measure text, or raster the whole window.
- A caret blink toggles the cached caret record and repaints that caret rectangle. It does not call `LayoutDocument`.
- A scroll moves the cached frame and paints only the newly exposed band. It does not call `LayoutDocument`. A fixed element keeps a full-frame paint.
- A text edit inside a field, a checkbox, or a radio button rewrites that control's cached records and repaints its rectangle. It does not call `LayoutDocument`.
- A click that opens, hides, or restyles content, a marquee tick, or an animation changes geometry, so a new layout is allowed.
- The first frame and a width change call `LayoutDocument` because block widths follow the window. A height-only resize keeps the cached layout when no length depends on the viewport height, and paints only the newly exposed band. Later events reuse that result until the geometry changes.
- Unchanged regions are not walked, restyled, or remeasured.

`AdvanceFrame` in `Source/Runtime/FramePlan.h` is the only window refresh path. `LayoutDocument` remains the reference engine for tests and for a viewport or content change. A color hover, a caret blink, and a scroll must increment `dirtyRasterCount` or `fullRasterCount` without incrementing `layoutCount`.

Warn, through `WarnSlowPaths`, when a construct cannot stay on this path. The warning says the region stays on the general engine or the interpreter and will not have C++ performance. The page still compiles. Covered cases are marquee, CSS or SVG animation, and script syntax outside straight assignments (`eval`, `innerHTML`, `function`, a brace, or an arrow function). Do not silently put a hot path back on `LayoutDocument` or `MiniScript`.

Supported script statements compile as direct field writes. Anything else stays on `MiniScript` only behind that warning. Do not build a general JavaScript compiler, and do not claim C++ performance for the warned path.

Static data is immutable for the life of the process. Dynamic data is recomputed only from the input that changed: window size, hover, focus, caret phase, form value, or a live handler.

The filenames must be PascalCase. The functions, classes, and structs must be PascalCase. Variable names must be camelCase. Constants must be CAPITAL_CASE.

Use the MDN-Reference folder as the reference and implement elements one by one, then delete that element's reference file. For each element, write tests and define its default behavior so the result matches a web browser. Use C++ and the Makefile only. Use Python only for a download. The runtime must be independent of the operating system. The renderer must be independent of the operating system. Win32 is a host, not the product.

Give a warning when something will compile but can break behavior. Write tests so behavior and pixels stay browser-identical while the event path stays on the C++ frame plan above. Do not weaken a test, delete a reference check, or mark a slow path as fast to make the build green.
