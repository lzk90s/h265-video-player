#pragma once

#include <cstdint>
#include "libyuv.h"

namespace common {

/*
stride-跨距的解释
跨距就是指图像中的一行图像数据所占的存储空间的长度，它是一个大于等于图像宽度的内存对齐的长度。这样每次以行为基准读取数据的时候就能内存对齐
和ffmpeg相对应的就是AVFrame的linesize
*/

// RGB <-> BGR R和B互换
class RGBConverter {
public:
    static inline void Convert(uint8_t *src, uint32_t width, uint32_t height) noexcept {
        for (uint32_t i = 0; i < height; i++) {
            for (uint32_t j = 0; j < width; j++) {
                uint8_t buf = *src;
                *src        = *(src + 2);
                *(src + 2)  = buf;
                src += 3;
            }
        }
    }
};

// RGB24转I420
class BGR24ToI420Converter {
public:
    static void Convert(const uint8_t *src_rgb24, int src_stride_rgb24, uint8_t *dst_y, int dst_stride_y, uint8_t *dst_u, int dst_stride_u,
                        uint8_t *dst_v, int dst_stride_v, int width, int height) {
        // libyuv虽然函数名是RGB24，实际测试下来是BGR24
        libyuv::RGB24ToI420(src_rgb24, src_stride_rgb24, dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v, dst_stride_v, width, height);
    }
};

// I420转RGB24
class I420ToBGR24Converter {
public:
    static void Convert(const uint8_t *src_y, int src_stride_y, const uint8_t *src_u, int src_stride_u, const uint8_t *src_v, int src_stride_v,
                        uint8_t *dst_rgb24, int dst_stride_rgb24, int width, int height) {
        // libyuv虽然函数名是RGB24，实际测试下来，格式是BGR，详见下面的test
        libyuv::I420ToRGB24(src_y, src_stride_y, src_u, src_stride_u, src_v, src_stride_v, dst_rgb24, dst_stride_rgb24, width, height);
    }
};

#ifdef USE_TEST
    #include <stdio.h>
    #include "opencv/cv.h"
    #include "opencv2/opencv.hpp"
void test() {
    int w = 10;
    int h = 20;
    cv::Mat img(h, w, CV_8UC3);
    uint8_t *src = img.data;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // opencv默认是BGR24格式
            uint8_t b = 50, g = 100, r = 255;
            *(src + 0) = b; // b
            *(src + 1) = g; // g
            *(src + 2) = r; // r
            src += 3;
        }
    }

    // bgr -> yuv420p
    Mat yuv;
    cvtColor(img, yuv, CV_BGR2YUV_I420);

    // yuv420p的排列格式是: yyyyyyyy uu uu
    uint8_t *y, *u, *v;
    y        = yuv.data;
    stride_y = w;
    u        = y + w * h;
    stride_u = w / 2;
    v        = u + w * h / 4;
    stride_v = w / 2;

    // libyuv yuv420p->bgr24
    Mat dst(h, w, CV_8UC3);
    I420ToBGR24Converter::Convert(y, stride_y, u, stride_u, v, stride_v, dst.data, w * 3, w, h);

    //输出数据，发现是BGR24格式
    src = dst.data;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            uint8_t b = *(src + 0); // b
            uint8_t g = *(src + 1); // g
            uint8_t r = *(src + 2); // r
            src += 3;
            printf("%u %u %u\n", b, g, r);
        }
    }
}
#endif

} // namespace common