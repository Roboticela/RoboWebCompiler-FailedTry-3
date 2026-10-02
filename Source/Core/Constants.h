#pragma once

namespace RoboWeb {

constexpr int DEFAULT_FONT_SIZE_PX = 16;
constexpr int DEFAULT_VIEWPORT_WIDTH_PX = 960;
constexpr int DEFAULT_VIEWPORT_HEIGHT_PX = 640;
constexpr int MIN_VIEWPORT_PX = 1;
constexpr int MAX_VIEWPORT_PX = 8192;
constexpr int BODY_MARGIN_PX = 8;
constexpr int LIST_PADDING_PX = 40;
constexpr int BLOCKQUOTE_SIDE_PX = 40;
constexpr int FIGURE_SIDE_PX = 40;
constexpr int DESCRIPTION_OFFSET_PX = 40;
constexpr int TABLE_BORDER_SPACING_PX = 2;
constexpr int CELL_PADDING_PX = 1;
constexpr int LINE_HEIGHT_NUMERATOR = 6;
constexpr int LINE_HEIGHT_DENOMINATOR = 5;
constexpr int SMALLER_FONT_MILLI = 833;
constexpr int LARGER_FONT_MILLI = 1200;
constexpr int SUB_SUP_SHIFT_MILLI = 250;
constexpr int CONTROL_PADDING_X_PX = 4;
constexpr int CONTROL_PADDING_Y_PX = 2;
constexpr int CONTROL_BORDER_PX = 1;
constexpr int DEFAULT_INPUT_SIZE = 20;
constexpr int DEFAULT_TEXTAREA_COLS = 20;
constexpr int DEFAULT_TEXTAREA_ROWS = 2;
constexpr int DEFAULT_REPLACED_WIDTH_PX = 300;
constexpr int DEFAULT_REPLACED_HEIGHT_PX = 150;
constexpr int AUDIO_CONTROL_HEIGHT_PX = 54;
constexpr int MAX_GEOLOCATION_CONTROLS = 3;
constexpr int TAB_WIDTH_COLUMNS = 8;
constexpr int HR_THICKNESS_PX = 2;
constexpr unsigned COLOR_CHANNELS = 4;

constexpr unsigned TRAIT_VOID = 1u << 0;
constexpr unsigned TRAIT_OBSOLETE = 1u << 1;
constexpr unsigned TRAIT_EXPERIMENTAL = 1u << 2;
constexpr unsigned TRAIT_RAW_TEXT = 1u << 3;
constexpr unsigned TRAIT_RC_DATA = 1u << 4;
constexpr unsigned TRAIT_PLAIN_TEXT = 1u << 5;

constexpr unsigned PRESENTATION_BOLD = 1u << 0;
constexpr unsigned PRESENTATION_ITALIC = 1u << 1;
constexpr unsigned PRESENTATION_UNDERLINE = 1u << 2;
constexpr unsigned PRESENTATION_STRIKE = 1u << 3;
constexpr unsigned PRESENTATION_MONOSPACE = 1u << 4;
constexpr unsigned PRESENTATION_CENTER = 1u << 5;
constexpr unsigned PRESENTATION_NOWRAP = 1u << 6;
constexpr unsigned PRESENTATION_PRE = 1u << 7;
constexpr unsigned PRESENTATION_MARK = 1u << 8;
constexpr unsigned PRESENTATION_SUB = 1u << 9;
constexpr unsigned PRESENTATION_SUP = 1u << 10;
constexpr unsigned PRESENTATION_QUOTE = 1u << 11;

constexpr int FONT_MILLI_H1 = 2000;
constexpr int FONT_MILLI_H2 = 1500;
constexpr int FONT_MILLI_H3 = 1170;
constexpr int FONT_MILLI_H4 = 1000;
constexpr int FONT_MILLI_H5 = 830;
constexpr int FONT_MILLI_H6 = 670;
constexpr int MARGIN_MILLI_H1 = 670;
constexpr int MARGIN_MILLI_H2 = 830;
constexpr int MARGIN_MILLI_H3 = 1000;
constexpr int MARGIN_MILLI_H4 = 1330;
constexpr int MARGIN_MILLI_H5 = 1670;
constexpr int MARGIN_MILLI_H6 = 2330;
constexpr int MARGIN_MILLI_BLOCK = 1000;
constexpr int MARGIN_MILLI_HR = 500;

}
