/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <image/ImageOps.h>

#include <math/vec3.h>

#include <utils/Panic.h>

#include <algorithm>

using namespace math;

namespace image {

LinearImage vectorsToColors(const LinearImage& image) {
    ASSERT_PRECONDITION(image.getChannels() == 3, "Must be a 3-channel image.");
    const uint32_t width = image.getWidth(), height = image.getHeight();
    LinearImage result(width, height, 3);
    auto src = (float3 const*) image.get();
    auto dst = (float3*) result.get();
    for (uint32_t n = 0; n < width * height; ++n) {
        dst[n] = 0.5f * (src[n] + float3(1));
    }
    return result;
}

LinearImage hstack(std::initializer_list<LinearImage> images) {
    size_t count = images.end() - images.begin();
    return hstack(images.begin(), count);
}

LinearImage hstack(const LinearImage* first, size_t count) {
    ASSERT_PRECONDITION(count > 0, "Must supply one or more images for stacking.");

    // Compute the final size and allocate memory.
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t nchannels = 0;
    for (size_t c = 0; c < count; ++c) {
        const auto& img = first[c];
        width += img.getWidth();
        if (height == 0) {
            height = img.getHeight();
        } else {
            ASSERT_PRECONDITION(height == img.getHeight(), "Inconsistent heights.");
        }
        if (nchannels == 0) {
            nchannels = img.getChannels();
        } else {
            ASSERT_PRECONDITION(nchannels == img.getChannels(), "Inconsistent channels.");
        }
    }
    LinearImage result(width, height, nchannels);

    // Copy over each row of each source image.
    float* dst = result.get();
    for (int32_t row = 0; row < height; ++row) {
        for (size_t c = 0; c < count; ++c) {
            const auto& img = first[c];
            uint32_t swidth = img.getWidth();
            float const* src = img.get() + row * swidth * nchannels;
            memcpy(dst, src, swidth * nchannels * sizeof(float));
            dst += swidth * nchannels;
        }
    }
    return result;
}

LinearImage vstack(std::initializer_list<LinearImage> images) {
    size_t count = images.end() - images.begin();
    return vstack(images.begin(), count);
}

// To stack images vertically, we transpose them individually, then hstack them, then transpose the
// result. This is incredibly lazy, but since we use row-major ordering, copying columns would be
// really painful.
LinearImage vstack(const LinearImage* first, size_t count) {
    ASSERT_PRECONDITION(count > 0, "Must supply one or more images for stacking.");
    std::unique_ptr<LinearImage[]> flipped(new LinearImage[count]);
    int i = 0;
    for (size_t c = 0; c < count; ++c) {
        flipped[i++] = transpose(first[c]);
    }
    auto result = hstack(flipped.get(), count);
    return transpose(result);
}

LinearImage combineChannels(std::initializer_list<LinearImage> images) {
    size_t count = images.end() - images.begin();
    return combineChannels(images.begin(), count);
}

LinearImage combineChannels(LinearImage const* img, size_t count) {
    ASSERT_PRECONDITION(count > 0, "Must supply one or more image planes for combining.");
    const uint32_t width = img[0].getWidth();
    const uint32_t height = img[0].getHeight();
    for (size_t c = 0; c < count; ++c) {
        const LinearImage& plane = img[c];
        ASSERT_PRECONDITION(plane.getWidth() == width, "Planes must all have same width.");
        ASSERT_PRECONDITION(plane.getHeight() == height, "Planes must all have same height.");
        ASSERT_PRECONDITION(plane.getChannels() == 1, "Planes must be single channel.");
    }
    LinearImage result(width, height, (uint32_t) count);
    float* dst = result.get();
    uint32_t sindex = 0, dindex = 0;
    while (dindex < width * height * count) {
        for (size_t c = 0; c < count; ++c, ++dindex) {
            const LinearImage& plane = img[c];
            float const* src = plane.get();
            dst[dindex] = src[sindex];
        }
        ++sindex;
    }
    return result;
}

// The transpose operation does not simply set a flag, it performs actual movement of data. This is
// very handy for separable filters because it (a) improves cache coherency in the second pass, and
// (b) allows the client to consume columns in the same way that it consumes rows. Our
// implementation does not support in-place transposition but it is simple and robust for non-square
// images.
LinearImage transpose(const LinearImage& image) {
    const uint32_t width = image.getWidth();
    const uint32_t height = image.getHeight();
    const uint32_t channels = image.getChannels();
    LinearImage result(height, width, channels);
    float const* source = image.get();
    float* target = result.get();
    for (uint32_t n = 0; n < width * height; ++n) {
        const uint32_t i = n / width;
        const uint32_t j = n % width;
        float const* src = source + channels * n;
        float* dst = target + channels * (height * j + i);
        for (uint32_t c = 0; c < channels; ++c) {
            dst[c] = src[c];
        }
    }
    return result;
}

LinearImage cropRegion(const LinearImage& image, uint32_t left, uint32_t top, uint32_t right,
        uint32_t bottom) {
    uint32_t width = right - left;
    uint32_t height = bottom - top;
    uint32_t channels = image.getChannels();
    LinearImage result(width, height, channels);
    float const* source = image.get(left, top);
    float* target = result.get();
    for (int32_t row = 0; row < height; ++row) {
        memcpy(target, source, width * channels * sizeof(float));
        target += width * channels;
        source += image.getWidth() * channels;
    }
    return result;
}

int compare(const LinearImage& a, const LinearImage& b, float epsilon) {
    auto w = a.getWidth();
    auto h = a.getHeight();
    auto c = a.getChannels();
    if (b.getWidth() != w || b.getHeight() != h || b.getChannels() != c) {
        return -1;
    }
    float const* adata = a.get();
    float const* bdata = b.get();
    return std::lexicographical_compare(adata, adata + w * h * c, bdata, bdata + w * h * c,
            [epsilon](float x, float y) { return x >= y - epsilon && x <= y + epsilon; });
}

} // namespace image
