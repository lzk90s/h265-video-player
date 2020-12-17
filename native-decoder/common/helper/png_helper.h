#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <memory>

namespace common {

#define PNG_BYTES_TO_CHECK 8
#define HAVE_ALPHA         1
#define NOT_HAVE_ALPHA     0

#if 0

class Png2BgrConverter {
public:
    Png2BgrConverter() {
        height = 0;
        width = 0;
        rgb_buffer = nullptr;
    }

    unsigned char *GetImgBuffer() {
        return rgb_buffer.get();
    }

    int GetWidth() {
        return width;
    }

    int GetHeight() {
        return height;
    }

    int Convert(unsigned char* pngBuffer, int pngSize) {
        png_structp png_ptr; //png文件句柄
        png_infop   info_ptr;//png图像信息句柄
        int ret;
        FILE *fp;
        if (isPng(&fp, filename) != 0) {
            printf("file is not png ...\n");
            return -1;
        }
        printf("launcher[%s] ...\n", PNG_LIBPNG_VER_STRING); //打印当前libpng版本号

        //1: 初始化libpng的数据结构 :png_ptr, info_ptr
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        info_ptr = png_create_info_struct(png_ptr);

        //2: 设置错误的返回点
        setjmp(png_jmpbuf(png_ptr));
        rewind(fp); //等价fseek(fp, 0, SEEK_SET);

        //3: 把png结构体和文件流io进行绑定
        png_init_io(png_ptr, fp);
        //4:读取png文件信息以及强转转换成RGBA:8888数据格式
        png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_EXPAND, 0); //读取文件信息
        int channels, color_type;
        channels = png_get_channels(png_ptr, info_ptr); //通道数量
        color_type = png_get_color_type(png_ptr, info_ptr);//颜色类型
        out->bit_depth = png_get_bit_depth(png_ptr, info_ptr);//位深度
        out->width = png_get_image_width(png_ptr, info_ptr);//宽
        out->height = png_get_image_height(png_ptr, info_ptr);//高

        //if(color_type == PNG_COLOR_TYPE_PALETTE)
        //  png_set_palette_to_rgb(png_ptr);//要求转换索引颜色到RGB
        //if(color_type == PNG_COLOR_TYPE_GRAY && out->bit_depth < 8)
        //  png_set_expand_gray_1_2_4_to_8(png_ptr);//要求位深度强制8bit
        //if(out->bit_depth == 16)
        //  png_set_strip_16(png_ptr);//要求位深度强制8bit
        //if(png_get_valid(png_ptr,info_ptr,PNG_INFO_tRNS))
        //  png_set_tRNS_to_alpha(png_ptr);
        //if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        //  png_set_gray_to_rgb(png_ptr);//灰度必须转换成RG
        printf("channels = %d color_type = %d bit_depth = %d width = %d height = %d ...\n",
               channels, color_type, out->bit_depth, out->width, out->height);

        int i, j, k;
        int size, pos = 0;
        int temp;

        //5: 读取实际的rgb数据
        png_bytepp row_pointers; //实际存储rgb数据的buf
        row_pointers = png_get_rows(png_ptr, info_ptr); //也可以分别每一行获取png_get_rowbytes();
        size = out->width * out->height; //申请内存先计算空间
        if (channels == 4 || color_type == PNG_COLOR_TYPE_RGB_ALPHA) { //判断是24位还是32位
            out->alpha_flag = HAVE_ALPHA; //记录是否有透明通道
            size *= (sizeof(unsigned char) * 4); //size = out->width * out->height * channel
            out->rgba = (png_bytep)malloc(size);
            if (NULL == out->rgba) {
                printf("malloc rgba faile ...\n");
                png_destroy_read_struct(&png_ptr, &info_ptr, 0);
                fclose(fp);
                return -1;
            }
            //从row_pointers里读出实际的rgb数据出来
            temp = channels - 1;
            for (i = 0; i < out->height; i++)
                for (j = 0; j < out->width * 4; j += 4)
                    for (k = temp; k >= 0; k--)
                        out->rgba[pos++] = row_pointers[i][j + k];
        } else if (channels == 3 || color_type == PNG_COLOR_TYPE_RGB) { //判断颜色深度是24位还是32位
            out->alpha_flag = NOT_HAVE_ALPHA;
            size *= (sizeof(unsigned char) * 3);
            out->rgba = (png_bytep)malloc(size);
            if (NULL == out->rgba) {
                printf("malloc rgba faile ...\n");
                png_destroy_read_struct(&png_ptr, &info_ptr, 0);
                fclose(fp);
                return -1;
            }
            //从row_pointers里读出实际的rgb数据
            temp = (3 * out->width);
            for (i = 0; i < out->height; i++) {
                for (j = 0; j < temp; j += 3) {
                    out->rgba[pos++] = row_pointers[i][j + 2];
                    out->rgba[pos++] = row_pointers[i][j + 1];
                    out->rgba[pos++] = row_pointers[i][j + 0];
                }
            }
        } else return -1;
        //6:销毁内存
        png_destroy_read_struct(&png_ptr, &info_ptr, 0);
        fclose(fp);
        //此时， 我们的out->rgba里面已经存储有实际的rgb数据了
        //处理完成以后free(out->rgba)
        return 0;
    }

private:
    int isPng(unsigned char* pngBuffer, int pngSize) { //检查是否png文件
        char checkheader[PNG_BYTES_TO_CHECK]; //查询是否png头
        *fp = fopen(filename, "rb");
        if (*fp == NULL) {
            printf("open failed ...1\n");
            return -1;
        }
        if (fread(checkheader, 1, PNG_BYTES_TO_CHECK, *fp) != PNG_BYTES_TO_CHECK) //读取png文件长度错误直接退出
            return 0;
        return png_sig_cmp(checkheader, 0, PNG_BYTES_TO_CHECK); //0正确, 非0错误
    }

private:
    std::unique_ptr<unsigned char[]> rgb_buffer;
    int width;
    int height;
};

#endif

} // namespace common
