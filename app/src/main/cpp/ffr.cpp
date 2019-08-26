#include "ffr.h"

#include <cmath>
#include <memory>

#include "utils.h"

using namespace gl_render_utils;

namespace {
    const std::string FFR_COMMON_SHADER_FORMAT = R"glsl(
        #version 300 es
        #extension GL_OES_EGL_image_external_essl3 : enable

        // https://www.shadertoy.com/view/3l2GRR

        // MIT License
        //
        // Copyright (c) 2019 Riccardo Zaglia
        //
        // Permission is hereby granted, free of charge, to any person obtaining a copy
        // of this software and associated documentation files (the "Software"), to deal
        // in the Software without restriction, including without limitation the rights
        // to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
        // copies of the Software, and to permit persons to whom the Software is
        // furnished to do so, subject to the following conditions:
        //
        // The above copyright notice and this permission notice shall be included in all
        // copies or substantial portions of the Software.
        //
        // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
        // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
        // FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
        // AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
        // LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
        // OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
        // SOFTWARE.

        const uvec2 TARGET_RESOLUTION = uvec2(%u, %u);
        const uvec2 OPTIMIZED_RESOLUTION = uvec2(%u, %u);
        const vec2 FOCUS_POSITION = vec2(%f, %f);
        const vec2 FOVEATION_SCALE = vec2(%f, %f);
        const vec2 BOUND_START = vec2(%f, %f);
        const vec2 DISTORTED_SIZE = vec2(%f, %f);
        const vec2 RESOLUTION_SCALE = vec2(TARGET_RESOLUTION) / vec2(OPTIMIZED_RESOLUTION);


        //Choose one distortion function:

        // ARCTANGENT: good for fixed foveated rendering
        const float EPS = 0.000001;
        #define INVERSE_DISTORTION_FN(a)   atan(a)
        #define INV_DIST_DERIVATIVE(a)     atanDerivative(a)
        float atanDerivative(float a) {
            return 1. / (a * a + 1.);
        }

        // HYPERBOLIC TANGENT: good compression but the periphery is too squished
        //const float EPS = 0.000001;
        //#define INVERSE_DISTORTION_FN(a)   tanh(a)
        //#define INV_DIST_DERIVATIVE(a)     tanhDerivative(a)
        //float tanhDerivative(float a) {
        //    float tanh_a = tanh(a);
        //    return 1. - tanh_a * tanh_a;
        //}

        // POW: good for tracked foveated rendering
        //const float POWER = 4. * sqrt(FOVEATION_SCALE.x * FOVEATION_SCALE.y);
        //const float EPS = 0.01;
        //#define INVERSE_DISTORTION_FN(a)   pow(a, 1. / POWER)
        //#define INV_DIST_DERIVATIVE(a)     (pow(a, 1. / POWER - 1.) / POWER)

        // Other functions for distortion:
        // https://en.wikipedia.org/wiki/Sigmoid_function


        vec2 InverseRadialDistortion(vec2 xy) {
            vec2 scaledXY = xy * FOVEATION_SCALE;
            float scaledRadius = length(scaledXY);
            return INVERSE_DISTORTION_FN(scaledRadius) * scaledXY / scaledRadius;
        }

        // Inverse radial distortion derivative wrt length(xy)
        vec2 InverseRadialDistortionDerivative(vec2 xy) {
            vec2 scaledXY = xy * FOVEATION_SCALE;
            float scaledRadius = length(scaledXY);
            return (INV_DIST_DERIVATIVE(scaledRadius) * FOVEATION_SCALE) * scaledXY / scaledRadius;
        }

        vec2 Undistort(vec2 uv) {
            return (InverseRadialDistortion(uv - FOCUS_POSITION) - BOUND_START) / DISTORTED_SIZE;
        }

        vec2 UndistortRadialDerivative(vec2 uv) {
            return InverseRadialDistortionDerivative(uv - FOCUS_POSITION) / DISTORTED_SIZE;
        }

        vec2 GetFilteringWeight2D(vec2 uv) {
            float radialExpansion = length(UndistortRadialDerivative(uv));
            vec2 contraction = 1. / (radialExpansion * RESOLUTION_SCALE);

            vec2 modifiedContraction = contraction - 1. / contraction; // -> ?

            return max(modifiedContraction, EPS);
        }

        vec2 TextureToEyeUV(vec2 textureUV, bool isRightEye) {
            // flip distortion horizontally for right eye
            // left: x * 2; right: (1 - x) * 2
            return vec2((textureUV.x + float(isRightEye) * (1. - 2. * textureUV.x)) * 2., textureUV.y);
        }

        vec2 EyeToTextureUV(vec2 eyeUV, bool isRightEye) {
            // left: x / 2; right 1 - (x / 2)
            return vec2(eyeUV.x / 2. + float(isRightEye) * (1. - eyeUV.x), eyeUV.y);
        }
    )glsl";

    const std::string UNDISTORT_FRAGMENT_SHADER = R"glsl(
        uniform samplerExternalOES tex0;
        in vec2 uv;
        out vec4 color;
        void main() {
            bool isRightEye = uv.x > 0.5;
            vec2 undistedUV = Undistort(TextureToEyeUV(uv, isRightEye));
            color = texture(tex0, EyeToTextureUV(undistedUV, isRightEye));

//            color = texture(tex0, uv);
        }
    )glsl";

    const std::string SHARPENING_FRAGMENT_SHADER = R"glsl(
        const float SHARPEN_STRENGTH = 0.5;
        const vec2 SHARPEN_SCALE = SHARPEN_STRENGTH / vec2(TARGET_RESOLUTION);

        uniform sampler2D tex0;
        in vec2 uv;
        out vec4 color;
        void main() {
            //vec2 sharpenWeight = GetFilteringWeight2D(TextureToEyeUV(uv, uv.x > 0.5));
            //vec2 delta = SHARPEN_SCALE * sharpenWeight;

            //vec3 currentColor = texture(tex0, uv).rgb;
            //vec3 leftColor = texture(tex0, uv - vec2(delta.x / 2., 0.)).rgb;
            //vec3 rightColor = texture(tex0, uv + vec2(delta.x / 2., 0.)).rgb;
            //vec3 downColor = texture(tex0, uv - vec2(0., delta.y)).rgb;
            //vec3 upColor = texture(tex0, uv + vec2(0., delta.y)).rgb;

            //vec3 finalColor = 5. * currentColor + -1. * (leftColor + rightColor + downColor + upColor);
            //color = vec4(finalColor, 1.);

            color = texture(tex0, uv);
        }
    )glsl";

    const float DEG_TO_RAD = (float) M_PI / 180;

#define INVERSE_DISTORTION_FN(a) atan(a);
    const float INVERSE_DISTORTION_DERIVATIVE_IN_0 = 1; // d(atan(0))/dx = 1

    float CalcBoundStart(float focusPos, float fovScale) {
        return INVERSE_DISTORTION_FN(-focusPos * fovScale);
    }

    float CalcBoundEnd(float focusPos, float fovScale) {
        return INVERSE_DISTORTION_FN((1.f - focusPos) * fovScale);
    }

    float CalcDistortedDimension(float focusPos, float fovScale) {
        float boundEnd = CalcBoundEnd(focusPos, fovScale);
        float boundStart = CalcBoundStart(focusPos, fovScale);
        return boundEnd - boundStart;
    }

    float CalcOptimalDimension(float scale, float distortedDim, float originalDim) {
        float inverseDistortionDerivative = INVERSE_DISTORTION_DERIVATIVE_IN_0 * scale;
        float gradientOnFocus = inverseDistortionDerivative / distortedDim;
        return originalDim / gradientOnFocus;
    }

    struct FoveationVars {
        uint32_t targetEyeWidth;
        uint32_t targetEyeHeight;
        uint32_t optimizedEyeWidth;
        uint32_t optimizedEyeHeight;
        float focusPositionX;
        float focusPositionY;
        float foveationScaleX;
        float foveationScaleY;
        float boundStartX;
        float boundStartY;
        float distortedWidth;
        float distortedHeight;
    };

    FoveationVars CalculateFoveationVars(FFRData data) {
        float widthf = data.eyeWidth;
        float heightf = data.eyeHeight;

        // left and right side screen plane width with unit focal
        float leftHalfWidth = tan(data.leftEyeFov.left * DEG_TO_RAD);
        float rightHalfWidth = tan(data.leftEyeFov.right * DEG_TO_RAD);
        // foveated center X assuming screen plane with unit width
        float focusPositionX = leftHalfWidth / (leftHalfWidth + rightHalfWidth);

        // NB: swapping top/bottom fov
        float topHalfHeight = tan(data.leftEyeFov.bottom * DEG_TO_RAD);
        float bottomHalfHeight = tan(data.leftEyeFov.top * DEG_TO_RAD);
        float focusPositionY = topHalfHeight / (topHalfHeight + bottomHalfHeight);

        //calculate foveation scale such as the "area" of the foveation region remains equal to (mFoveationStrengthMean)^2
        // solve for {foveationScaleX, foveationScaleY}:
        // /{ foveationScaleX * foveationScaleY = (mFoveationStrengthMean)^2
        // \{ foveationScaleX / foveationScaleY = 1 / mFoveationShapeRatio
        // then foveationScaleX := foveationScaleX / (targetEyeWidth / targetEyeHeight) to compensate for non square frame.
        float scaleCoeff = data.foveationStrengthMean * sqrt(data.foveationShapeRatio);
        float foveationScaleX = scaleCoeff / data.foveationShapeRatio / (widthf / heightf);
        float foveationScaleY = scaleCoeff;

        float boundStartX = CalcBoundStart(focusPositionX, foveationScaleX);
        float boundStartY = CalcBoundStart(focusPositionY, foveationScaleY);

        float distortedWidth = CalcDistortedDimension(focusPositionX, foveationScaleX);
        float distortedHeight = CalcDistortedDimension(focusPositionY, foveationScaleY);

        float optimizedEyeWidth = CalcOptimalDimension(foveationScaleX, distortedWidth, widthf);
        float optimizedEyeHeight = CalcOptimalDimension(foveationScaleY, distortedHeight, heightf);

        return {data.eyeWidth, data.eyeHeight, (uint32_t) optimizedEyeWidth,
                (uint32_t) optimizedEyeHeight, focusPositionX, focusPositionY, foveationScaleX,
                foveationScaleY, boundStartX, boundStartY, distortedWidth, distortedHeight};
    }
}


FFR::FFR(Texture *inputSurface)
        : mInputSurface(inputSurface) {
}

void FFR::Initialize(FFRData ffrData) {

    mDistortedTexture = std::make_unique<Texture>(false, ffrData.eyeWidth * 2, ffrData.eyeHeight,
                                                  GL_RGB8);
    mSharpenedTexture = std::make_unique<Texture>(false, ffrData.eyeWidth * 2, ffrData.eyeHeight,
                                                  GL_RGB8);

    auto fv = CalculateFoveationVars(ffrData);
    auto ffrCommonShaderStr = string_format(FFR_COMMON_SHADER_FORMAT,
                                            fv.targetEyeWidth, fv.targetEyeHeight,
                                            fv.optimizedEyeWidth, fv.optimizedEyeHeight,
                                            fv.focusPositionX, fv.focusPositionY,
                                            fv.foveationScaleX, fv.foveationScaleY,
                                            fv.boundStartX, fv.boundStartY,
                                            fv.distortedWidth, fv.distortedHeight);
    auto undistortShaderStr = ffrCommonShaderStr + UNDISTORT_FRAGMENT_SHADER;
    auto sharpeningShaderStr = ffrCommonShaderStr + SHARPENING_FRAGMENT_SHADER;

    mUndistortPipeline.reset(new RenderPipeline({mInputSurface}, undistortShaderStr,
                                                mDistortedTexture.get()));
    mSharpeningPipeline.reset(new RenderPipeline({mDistortedTexture.get()}, sharpeningShaderStr,
                                                 mSharpenedTexture.get()));
}

void FFR::Render() {
    mUndistortPipeline->Render();
    mSharpeningPipeline->Render();
}
