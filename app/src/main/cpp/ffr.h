#pragma once

#include <memory>
#include <vector>

#include "gl_render_utils/render_pipeline.h"
#include "packet_types.h"


enum FFR_MODE {
    FFR_MODE_DISABLED = 0,
    FFR_MODE_SLICES = 1,
    FFR_MODE_WARP = 2,
};

struct FFRData {
    FFR_MODE mode;
    uint32_t eyeWidth;
    uint32_t eyeHeight;
    EyeFov leftEyeFov;
    float foveationStrengthMean;
    float foveationShapeRatio;
};

class FFR {
public:
    FFR(gl_render_utils::Texture *inputSurface);

    void Initialize(FFRData ffrData);

    void Render();

    gl_render_utils::Texture *GetOutputTexture() { return mExpandedTexture.get(); }

private:

    FFR_MODE mMode;

    gl_render_utils::Texture *mInputSurface;
    std::unique_ptr<gl_render_utils::Texture> mExpandedTexture;

    std::vector<std::unique_ptr<gl_render_utils::RenderPipeline>> mPipelines;
};
