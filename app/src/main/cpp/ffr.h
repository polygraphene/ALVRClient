#pragma once

#include <memory>

#include "gl_render_utils/render_pipeline.h"
#include "packet_types.h"


struct FFRData {
    uint32_t eyeWidth;
    uint32_t eyeHeight;
    EyeFov leftEyeFov;
    float foveationStrengthMean, foveationShapeRatio;
};

class FFR {
public:
    FFR(gl_render_utils::Texture *inputSurface);

    // targetWidth, targetHeight: rendering resolution on server before distortion
    void Initialize(FFRData ffrData);

    void Render();

    gl_render_utils::Texture *GetOutputTexture() { return mDistortedTexture.get(); }

private:

    gl_render_utils::Texture *mInputSurface;
    std::unique_ptr<gl_render_utils::Texture> mDistortedTexture;
    std::unique_ptr<gl_render_utils::Texture> mSharpenedTexture;

    std::unique_ptr<gl_render_utils::RenderPipeline> mUndistortPipeline;
    std::unique_ptr<gl_render_utils::RenderPipeline> mSharpeningPipeline;
};
