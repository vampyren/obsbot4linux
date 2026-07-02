// Preview capture resolutions, indexed by AppSettings::previewResIndex.
// Shared by CameraController (STATUS label / persistence) and PreviewEngine
// (QCameraFormat selection) so the two can never drift apart.
//
// Every entry maps to a REAL MJPG mode the Tiny 3 reports via
// `v4l2-ctl --list-formats-ext` (validated on hardware, fw 6.6.9.1):
// MJPG 1920x1080 @ 15–120, 3840x2160 @ 15–30, 1280x720 @ 15–120 (+ 1280x960,
// 1920x1440), plus a YUYV 640x360/640x480 fallback we don't use.
#pragma once

struct PreviewRes { const char *label; int w; int h; int fps; };

inline constexpr PreviewRes kPreviewRes[] = {
    {"1080p30", 1920, 1080, 30},
    {"1080p60", 1920, 1080, 60},
    {"720p60",  1280, 720,  60},
    {"4K30",    3840, 2160, 30},
};
inline constexpr int kPreviewResCount = int(sizeof(kPreviewRes) / sizeof(kPreviewRes[0]));
