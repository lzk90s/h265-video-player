#------------ 编译video-dispose ------------
FROM registry.cn-hangzhou.aliyuncs.com/gosun/brpc_opencv_ffmpeg:x86_64-3.0.1 as base_builder


RUN apt-get update --fix-missing && \
  apt-get install -y  g++-mingw-w64-x86-64 && \
  apt-get autoclean && apt-get clean

#编译
ADD . /root/work
RUN cd /root/work && \
  #build windows
  xmake f --cross=x86_64-w64-mingw32- --plat=mingw32 && \
  xmake && \
  #build linux
  xmake f --cross= --plat=linux && \
  xmake

#------------ 生成runtime包 ------------
FROM alpine:latest as runtime


##拷贝编译结果
COPY --from=base_builder /root/work/build/linux/x86_64/release/* /usr/local/bin/
COPY --from=base_builder /root/work/build/mingw32/x86_64/release/* /usr/local/bin/
