# web-video-player

web 视频播放器（fork from [WasmVideoPlayer](https://github.com/sonysuqin/WasmVideoPlayer) ）

相比 WasmVideoPlayer，做了以下修改

- 用 yarn 管理工程依赖，添加 eslint
- 代码结构调整，公共部分抽取，js 使用 class 方式
- 去掉 wasm 部分，改为通过 websocket 调用 native-decoder，图片数据量比较大，websocket 需要做压缩
- 支持无音频视频播放

## 背景

浏览器不原生支持h265，有的方案是采用的web assembly方式，把ffmpeg编译为wasm来供js使用，但是这样性能较差，web assembly限制比较多，1080P的视频播放卡顿。
本方案把视频解码功能提取出来，编译为本地解码，js通过websocket把h264/h265视频数据给native-decoder, native-decoder解码后，通过websocket把YUV的视频数据返回给浏览器，
浏览器再进行显示，经测试，性能比wasm版本的要好，可以流畅播放H265的视频


## 功能

- [x] 支持实时流和文件播放
- [x] 支持 h264 和 h265 编码格式的视频
- [x] 解决 chrome 不支持 h265 的问题，需要配合 native-decoder 工程使用，比 wasm 版本的性能高，1080P 视频可以流畅播放
- [x] 无音频视频播放
- [ ] websocket 自动重连

## 使用方法

```bash
yarn add --global parcel
yarn install
yarn build && yarn serve
```

示例参考 example/index.html

浏览器访问 http://localhost:9080

## 注意

使用前，需要打开 native-decoder 程序

## 问题

经过测试，虽然可以流畅的播放1080P的视频，但CPU的占用率依然高，i5的cpu，chrome浏览器同时播放3-4路视频cpu占用率100%。
经分析，cpu占用高的原因是chrome是多进程架构，进程间是通过chrome的RPC通信的，视频数据要经过 native-decoder -> chrome网络服务 -> chrome主进程 -> chrome GPU进程，解码后的视频数据量大，
CPU消耗在大量的拷贝数据上。

基于以上分析，这个方案还是有局限性的。目前，在考虑另一个方案，解码和显示都不在chrome完成，在外部解码并显示，在chrome上遮一层视频播放框，绕过chrome的RPC机制。

新方案还在开发中
