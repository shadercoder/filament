/*
 * Copyright 2018 The Android Open Source Project
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
#include <image/ImageSampler.h>
#include <image/LinearImage.h>

#include <imageio/ImageEncoder.h>
#include <imageio/ImageDecoder.h>

#include <gtest/gtest.h>

#include <utils/Panic.h>
#include <utils/Path.h>
#include <math/vec3.h>

#include <fstream>
#include <string>
#include <sstream>

using std::istringstream;
using std::string;
using math::float3;
using std::swap;

class ImageTest : public testing::Test {};

enum class ComparisonMode {
    SKIP,
    COMPARE,
    UPDATE,
};

static ComparisonMode g_comparisonMode;
static utils::Path g_comparisonPath;

// Just for fun, define a tiny Ray-Sphere intersector, which we'll use to generate a reasonable
// normal map for testing purposes.
struct Ray { float3 orig, dir; };
struct Sphere { float3 center; float radius2; };
static bool intersect(Ray ray, Sphere sphere, float* t);

// Creates a "size x size" normal map that looks like a hemisphere embedded in a plane.
static image::LinearImage createNormalMap(uint32_t size);

// Creates a "size x size" height map that looks like a hemisphere embedded in a plane.
static image::LinearImage createDepthMap(uint32_t size);

// Creates a tiny monochrome image from a pattern string.
static image::LinearImage createImageFromAscii(const string& pattern);

// Saves an image to disk or does a load-and-compare, depending on g_comparisonMode.
static void updateOrCompare(const image::LinearImage& limg, const utils::Path& fname);

// Subtracts two images, does an abs(), then normalizes such that min/max transform to 0/1.
static image::LinearImage diffImages(const image::LinearImage& a, const image::LinearImage& b);

TEST_F(ImageTest, LuminanceFilters) { // NOLINT
    auto tiny = createImageFromAscii("000 010 000");
    ASSERT_EQ(tiny.getWidth(), 3);
    ASSERT_EQ(tiny.getHeight(), 3);
    auto src = transpose(createImageFromAscii("01 23 45"));
    auto ref = createImageFromAscii("024 135");
    ASSERT_EQ(src.getWidth(), 3);
    ASSERT_EQ(src.getHeight(), 2);
    for (int i = 0; i < 6; i++) {
        EXPECT_FLOAT_EQ(src.get()[i], ref.get()[i]);
    }
    auto row = createImageFromAscii("010");
    auto mag1 = resampleImage(row, 6, 1, image::Filter::HERMITE);
    ASSERT_EQ(mag1.getWidth(), 6);
    ASSERT_EQ(mag1.getHeight(), 1);
    auto mag2 = resampleImage(row, 7, 2, image::Filter::HERMITE);
    ASSERT_EQ(mag2.getWidth(), 7);
    ASSERT_EQ(mag2.getHeight(), 2);
    auto box = resampleImage(tiny, 6, 6, image::Filter::BOX);
    auto nearest = resampleImage(tiny, 6, 6, image::Filter::NEAREST);
    auto ref3 = createImageFromAscii("000000 000000 001100 001100 000000 000000");
    for (int i = 0; i < 36; i++) {
        EXPECT_FLOAT_EQ(box.get()[i], ref3.get()[i]);
        EXPECT_FLOAT_EQ(nearest.get()[i], ref3.get()[i]);
    }
    auto grays0 = resampleImage(tiny, 100, 100, image::Filter::GAUSSIAN_SCALARS);
    auto mag3 = transpose(resampleImage(tiny, 32, 8, image::Filter::GAUSSIAN_SCALARS));
    auto grays1 = resampleImage(mag3, 100, 100, image::Filter::NEAREST);
    updateOrCompare(image::hstack({grays0, grays1}), "grays.png");
}

TEST_F(ImageTest, ColorFilters) { // NOLINT
    image::LinearImage images[3];

    // Test color space with a classic RED => GREEN color gradient.
    images[0] = createImageFromAscii("10");
    images[1] = createImageFromAscii("01");
    images[2] = createImageFromAscii("00");
    image::LinearImage color1 = combineChannels(images, 3);
    auto color2 = resampleImage(color1, 100, 100, image::Filter::NEAREST);
    auto color3 = resampleImage(color1, 100, 100, image::Filter::GAUSSIAN_SCALARS);
    auto color4 = resampleImage(color1, 100, 100, image::Filter::GAUSSIAN_SCALARS); // TODO linearize
    auto color5 = diffImages(color3, color4);

    // Try enlarging a 5x5 image using MITCHELL and LANCZOS filters.
    images[0] = createImageFromAscii("00000 02020 00100 02020 00000");
    images[1] = createImageFromAscii("00000 00000 00100 00000 00000");
    images[2] = createImageFromAscii("11111 10001 10101 10001 11111");
    image::LinearImage color6 = combineChannels(images, 3);
    auto color6b = resampleImage(color6, 100, 100, image::Filter::NEAREST);
    auto color7 = resampleImage(color6, 100, 100, image::Filter::MITCHELL);
    auto color8 = resampleImage(color6, 100, 100, image::Filter::LANCZOS);
    auto color9 = resampleImage(color6, 100, 100, image::Filter::GAUSSIAN_SCALARS);

    // Minification tests. Each of these do a nearest magnification afterwards for visualization
    // purposes.
    auto magnify = [](image::LinearImage img) {
        return resampleImage(img, 100, 100, image::Filter::NEAREST);
    };
    auto colora = magnify(resampleImage(color9, 3, 3, image::Filter::NEAREST));
    auto colorb = magnify(resampleImage(color9, 1, 1, image::Filter::NEAREST));
    auto colorc = magnify(resampleImage(color9, 3, 3, image::Filter::BOX));
    auto colord = magnify(resampleImage(color9, 1, 1, image::Filter::BOX));

    auto colors0 = image::hstack({color2, color3, color4, color5});
    auto colors1 = image::hstack({color6b, color7, color8, color9});
    auto colors2 = image::hstack({colora, colorb, colorc, colord});
    auto colors = image::vstack({colors0, colors1, colors2});

    // Even more minification tests....
    auto colore = magnify(resampleImage(colors,  5,  5, image::Filter::DEFAULT));
    auto colorf = magnify(resampleImage(colors, 50, 50, image::Filter::DEFAULT));
    auto colorg = magnify(resampleImage(colors,  5,  5, image::Filter::HERMITE));
    auto colorh = magnify(resampleImage(colors, 50, 50, image::Filter::HERMITE));
    auto colori = image::hstack({colore, colorf, colorg, colorh});
    colors = image::vstack({colors, colori});
    updateOrCompare(colors, "colors.png");
    ASSERT_EQ(colors.getWidth(), 400);
    ASSERT_EQ(colors.getHeight(), 400);

    // Test radius multiplier (blurring).
    image::ImageSampler sampler;
    sampler.horizontalFilter = sampler.verticalFilter = image::Filter::GAUSSIAN_SCALARS;
    sampler.filterRadiusMultiplier = 1;
    auto blurred0 = resampleImage(color6b, 100, 100, sampler);
    sampler.filterRadiusMultiplier = 10;
    auto blurred1 = resampleImage(color6b, 100, 100, sampler);
    sampler.filterRadiusMultiplier = 20;
    auto blurred2 = resampleImage(color6b, 100, 100, sampler);
    auto blurred3 = resampleImage(color6b, 101, 100, sampler);
    auto blurred4 = resampleImage(color6b,  99, 100, sampler);
    auto blurred = image::hstack({blurred0, blurred1, blurred2, blurred3, blurred4});

    // Test extraction via sourceRegion and subsequent blurring.
    sampler.sourceRegion = {0, 0.25f, 0.25f, 0.5f};
    sampler.filterRadiusMultiplier = 1;
    auto region0 = resampleImage(colors, 100, 100, sampler);
    sampler.filterRadiusMultiplier = 10;
    auto region1 = resampleImage(colors, 100, 100, sampler);
    sampler.filterRadiusMultiplier = 20;
    auto region2 = resampleImage(colors, 100, 100, sampler);
    auto region3 = resampleImage(colors, 101, 100, sampler);
    auto region4 = resampleImage(colors,  99, 100, sampler);
    auto region = image::hstack({region0, region1, region2, region3, region4});
    blurred = image::vstack({blurred, region});
    updateOrCompare(blurred, "blurred.png");

    // Sample the reddish-white pixel in the post-blurred image.
    image::SinglePixel result;
    computeSingleSample(colors, 0.375, 0.375, &result);
    auto red = int(result[0] * 255.0f);
    auto grn = int(result[1] * 255.0f);
    auto blu = int(result[2] * 255.0f);
    ASSERT_EQ(red, 207);
    ASSERT_EQ(grn, 200);
    ASSERT_EQ(blu, 200);
}

TEST_F(ImageTest, VectorFilters) { // NOLINT
    auto toColors = image::vectorsToColors;
    auto normals = createNormalMap(1024);
    auto wrong = resampleImage(toColors(normals), 16, 16, image::Filter::GAUSSIAN_SCALARS);
    auto right = toColors(resampleImage(normals, 16, 16, image::Filter::GAUSSIAN_NORMALS));
    auto diff = diffImages(wrong, right);
    auto atlas = image::hstack({wrong, right, diff});
    atlas = resampleImage(atlas, 300, 100, image::Filter::NEAREST);
    updateOrCompare(atlas, "normals.png");
}

TEST_F(ImageTest, DepthFilters) { // NOLINT
    auto depths = createDepthMap(1024);
    auto wrong = resampleImage(depths, 16, 16, image::Filter::GAUSSIAN_SCALARS);
    auto right = resampleImage(depths, 16, 16, image::Filter::MINIMUM);
    auto diff = diffImages(wrong, right);
    auto atlas = image::hstack({wrong, right, diff});
    atlas = resampleImage(atlas, 300, 100, image::Filter::NEAREST);
    updateOrCompare(atlas, "depths.png");
}

static void printUsage(const char* name) {
    std::string exec_name(utils::Path(name).getName());
    std::string usage(
            "TEST is a unit test runner for the Filament image library\n"
            "Usages:\n"
            "    TEST compare <path-to-ref-images> [gtest options]\n"
            "    TEST update  <path-to-ref-images> [gtest options]\n"
            "    TEST [gtest options]\n"
            "\n");
    const std::string from("TEST");
    for (size_t pos = usage.find(from); pos != std::string::npos; pos = usage.find(from, pos)) {
         usage.replace(pos, from.length(), exec_name);
    }
    printf("%s", usage.c_str());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (argc < 2) {
        std::cerr << "\nWARNING: No path provided, skipping reference image comparison.\n\n";
        g_comparisonMode = ComparisonMode::SKIP;
        return RUN_ALL_TESTS();
    }
    const string cmd = argv[1];
    if (cmd == "help") {
        printUsage(argv[0]);
        return 0;
    }
    if (cmd == "compare" || cmd == "update") {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        g_comparisonPath = argv[2];
    }
    if (cmd == "compare") {
        g_comparisonMode = ComparisonMode::COMPARE;
        return RUN_ALL_TESTS();
    }
    if (cmd == "update") {
        g_comparisonMode = ComparisonMode::UPDATE;
        return RUN_ALL_TESTS();
    }
    printUsage(argv[0]);
    return 1;
}

static image::LinearImage createNormalMap(uint32_t size) {
    image::LinearImage result(size, size, 3);
    auto vectors = (float3*) result.get();
    const float invsize = 1.0f / size;
    const Sphere sphere {
        .center = float3(0.5, 0.5, 0.0),
        .radius2 = 0.15
    };
    for (uint32_t n = 0; n < size * size; ++n) {
        const uint32_t row = n / size, col = n % size;
        const Ray ray {
            .orig = { (col + 0.5f) * invsize, 1.0f - (row + 0.5f) * invsize, 1 },
            .dir = {0, 0, -1}
        };
        float t;
        bool isect = intersect(ray, sphere, &t);
        if (isect) {
            float3 p = ray.orig + t * ray.dir;
            vectors[n] = normalize(p - sphere.center);
        } else {
            vectors[n] = {0, 0, 1};
        }
    }
    return result;
}

static image::LinearImage createDepthMap(uint32_t size) {
    image::LinearImage result(size, size, 1);
    auto depths = result.get();
    const float invsize = 1.0f / size;
    const Sphere sphere {
        .center = float3(0.5, 0.5, 0.0),
        .radius2 = 0.15
    };
    for (uint32_t n = 0; n < size * size; ++n) {
        const uint32_t row = n / size, col = n % size;
        const Ray ray {
            .orig = { (col + 0.5f) * invsize, 1.0f - (row + 0.5f) * invsize, 1 },
            .dir = {0, 0, -1}
        };
        float t;
        bool isect = intersect(ray, sphere, &t);
        if (isect) {
            float3 p = ray.orig + t * ray.dir;
            depths[n] = p.z;
        } else {
            depths[n] = 1;
        }
    }
    return result;
}

static image::LinearImage createImageFromAscii(const string& pattern) {
    uint32_t width = 0;
    uint32_t height = 0;
    string row;

    // Compute the required size.
    for (istringstream istream(pattern); istream >> row; ++height) {
        width = (uint32_t) row.size();
    }

    // Allocate the sequence of pixels.
    image::LinearImage result(width, height, 1);

    // Fill in the pixel data.
    istringstream istream(pattern);
    float* seq = result.get();
    for (int i = 0; istream >> row;) {
        for (char c : row) {
            seq[i++] = c - '0';
        }
    }
    return result;
}

static void updateOrCompare(const image::LinearImage& limg, const utils::Path& fname) {
    if (g_comparisonMode == ComparisonMode::SKIP) {
        return;
    }
    auto newrawdata = new uint8_t[limg.getHeight() * limg.getWidth() * 3 * sizeof(float)];
    std::unique_ptr<uint8_t[]> ptexels(newrawdata);
    auto dst = (math::float3*) newrawdata;
    float const* srcdata = limg.get();
    if (limg.getChannels() == 1) {
        for (uint32_t i = 0; i < limg.getWidth() * limg.getHeight(); i++) {
            dst[i].x = srcdata[i];
            dst[i].y = srcdata[i];
            dst[i].z = srcdata[i];
        }
    } else if (limg.getChannels() == 3) {
        for (uint32_t j = 0, i = 0; j < limg.getWidth() * limg.getHeight(); ++j) {
            dst[j].x = srcdata[i++];
            dst[j].y = srcdata[i++];
            dst[j].z = srcdata[i++];
        }
    } else {
        ASSERT_PRECONDITION(false, "This test only supports 3-channel and 1-channel images.");
    }
    size_t channels = 3, bpp = channels * sizeof(float), bpr = bpp * limg.getWidth();
    if (g_comparisonMode == ComparisonMode::UPDATE) {
        using image::ImageEncoder;
        std::ofstream out(g_comparisonPath + fname, std::ios::binary | std::ios::trunc);
        image::Image img(std::move(ptexels), limg.getWidth(), limg.getHeight(), bpr, bpp, channels);
        ImageEncoder::encode(out, ImageEncoder::Format::PNG_LINEAR, img, "", fname);
        return;
    }
    using image::ImageDecoder;
    const string fullpath = g_comparisonPath + fname;
    std::ifstream in(fullpath, std::ios::binary);
    ASSERT_PRECONDITION(in, "Unable to open: %s", fullpath.c_str());
    image::Image img = ImageDecoder::decode(in, g_comparisonPath + fname,
            ImageDecoder::ColorSpace::LINEAR);
    //ASSERT_PRECONDITION(compare(limg, img) == 0, "Image mismatch.");
}

static bool solve(float a, float b, float c, float *x0, float *x1) {
    float discr = b * b - 4 * a * c;
    if (discr < 0) return false;
    if (discr == 0) {
        *x0 = *x1 = -0.5f * b / a;
    } else {
        float q = (b > 0) ?  -0.5f * (b + sqrtf(discr)) :  -0.5f * (b - sqrtf(discr));
        *x0 = q / a;
        *x1 = c / q;
    }
    if (*x0 > *x1) swap(*x0, *x1);
    return true;
}

static bool intersect(Ray ray, Sphere sphere, float* t) {
    float t0, t1;
    float3 L = ray.orig - sphere.center;
    float a = dot(ray.dir, ray.dir);
    float b = 2 * dot(ray.dir, L);
    float c = dot(L, L) - sphere.radius2;
    if (!solve(a, b, c, &t0, &t1)) return false;
    if (t0 > t1) swap(t0, t1);
    if (t0 < 0) {
        t0 = t1;
        if (t0 < 0) return false;
    }
    *t = t0;
    return true;
}

static image::LinearImage diffImages(const image::LinearImage& a, const image::LinearImage& b) {
    const uint32_t width = a.getWidth(), height = a.getHeight(), nchan = a.getChannels();
    ASSERT_PRECONDITION(width == b.getWidth() && height == b.getHeight() &&
            nchan == b.getChannels(), "Images must have same shape.");
    image::LinearImage result(width, height, nchan);
    float* dst = result.get();
    float const* srca = a.get();
    float const* srcb = b.get();
    float largest = 0;
    float smallest = std::numeric_limits<float>::max();
    for (uint32_t n = 0; n < width * height * nchan; ++n) {
        float delta = std::abs(srca[n] - srcb[n]);
        largest = std::max(largest, delta);
        smallest = std::min(smallest, delta);
        dst[n] = delta;
    }
    const float scale = (largest == smallest) ? 1.0f : (1.0f / largest - smallest);
    for (uint32_t n = 0; n < width * height * nchan; ++n) {
        dst[n] = (dst[n] - smallest) * scale;
    }
    return result;
}
