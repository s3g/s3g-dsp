#pragma once

// Reuse the proven file/analysis/project-media workers on both supported GUIs.
#if defined(__APPLE__) || (defined(_WIN32) && (defined(S3G_ENABLE_VSTGUI_SAMPLE_FAMILY_GUI) || defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)))
#define S3G_SAMPLE_FILE_WORKER 1
#endif

#include "../../dsp/s3g_sample_asset.h"

#include <memory>
#include <string>

namespace s3g::sample_file {

// Decodes a sample path supplied by a portable file dialog. On Windows the
// UTF-8 path is converted to a native wide path before dr_wav opens it.
bool decodeWaveFile(const std::string& path,
    std::shared_ptr<const sample::SampleAsset>& assetOut,
    std::string& error);

} // namespace s3g::sample_file
