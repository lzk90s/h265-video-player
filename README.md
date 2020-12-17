# web-video-player

web 视频播放器（fork from [WasmVideoPlayer](https://github.com/sonysuqin/WasmVideoPlayer) ）

相比 WasmVideoPlayer，做了以下修改

- 用 yarn 管理工程依赖，添加 eslint
- 代码结构调整，公共部分抽取，js 使用 class 方式
- 去掉 wasm 部分，改为通过 websocket 调用 native-decoder，图片数据量比较大，websocket 需要做压缩
- 支持无音频视频播放

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
