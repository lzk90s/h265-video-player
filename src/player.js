import Logger from './logger'
import WebGLPlayer from './webgl'
import PCMPlayer from './pcm'

import {
  kProtoHttp, kGetFileInfoReq, kFeedDataReq, kOpenDecoderReq,
  kGetFileInfoRsp, kFileData, kInitDecoderRsp, kDownloadFileReq,
  kOpenDecoderRsp, kVideoFrame, kAudioFrame, kDecodeFinishedEvt,
  kSeekToRsp, kRequestDataEvt, kProtoWebsocket, kStartDecodingReq,
  kCloseDecoderReq, kInitDecoderReq, kPauseDecodingReq, kSeekToReq,
  kUninitDecoderReq, KDiscardDataReq
} from './constant'

// Decoder states.
const decoderStateIdle = 0
const decoderStateInitializing = 1
const decoderStateReady = 2
const decoderStateFinished = 3

// Player states.P
const playerStateIdle = 0
const playerStatePlaying = 1
const playerStatePausing = 2

// Constant.
const maxBufferTimeLength = 1.0
const downloadSpeedByteRateCoef = 2.0

// String.prototype.startWith = function (str) {
//   const reg = new RegExp(`^${str}`)
//   return reg.test(this)
// }

class FileInfo {
  constructor(url) {
    this.url = url
    this.size = 0
    this.offset = 0
    this.chunkSize = 65536
  }
}

class Player {
  constructor(downloadWorkerScript, decodeWorkerScript) {
    this.fileInfo = null
    this.pcmPlayer = null
    this.canvas = null
    this.webglPlayer = null
    this.callback = null
    this.waitHeaderLength = 524288
    this.duration = 0
    this.pixFmt = 0
    this.videoWidth = 0
    this.videoHeight = 0
    this.yLength = 0
    this.uvLength = 0
    this.beginTimeOffset = 0
    this.decoderState = decoderStateIdle
    this.playerState = playerStateIdle
    this.decoding = false
    this.decodeInterval = 5
    this.videoRendererTimer = null
    this.downloadTimer = null
    this.chunkInterval = 200
    this.downloadSeqNo = 0
    this.downloading = false
    this.downloadProto = kProtoHttp
    this.timeLabel = null
    this.timeTrack = null
    this.trackTimer = null
    this.trackTimerInterval = 500
    this.displayDuration = '00:00:00'
    this.audioEncoding = ''
    this.audioChannels = 0
    this.audioSampleRate = 0
    this.seeking = false // Flag to preventing multi seek from track.
    this.justSeeked = false // Flag to preventing multi seek from ffmpeg.
    this.urgent = false
    this.seekWaitLen = 524288 // Default wait for 512K, will be updated in onVideoParam.
    this.seekReceivedLen = 0
    this.loadingDiv = null
    this.buffering = false
    this.frameBuffer = []
    this.isStream = false
    this.streamReceivedLen = 0
    this.firstFrame = true
    this.fetchController = null
    this.streamPauseParam = null
    this.hasAudio = false
    this.logger = new Logger('Player')
    this.initDownloadWorker(downloadWorkerScript)
    this.initDecodeWorker(decodeWorkerScript)
  }

  initDownloadWorker(workerScript = null) {
    const self = this
    this.downloadWorker = new Worker(workerScript)
    this.downloadWorker.onmessage = function (evt) {
      const objData = evt.data
      switch (objData.t) {
        case kGetFileInfoRsp:
          self.onGetFileInfo(objData.i)
          break
        case kFileData:
          self.onFileData(objData.d, objData.s, objData.e, objData.q)
          break
      }
    }
  }

  initDecodeWorker(workerScript = null) {
    const self = this
    this.decodeWorker = new Worker(workerScript)
    this.decodeWorker.onmessage = function (evt) {
      const objData = evt.data
      switch (objData.t) {
        case kInitDecoderRsp:
          self.onInitDecoder(objData)
          break
        case kOpenDecoderRsp:
          self.onOpenDecoder(objData)
          break
        case kVideoFrame:
          self.onVideoFrame(objData)
          break
        case kAudioFrame:
          self.onAudioFrame(objData)
          break
        case kDecodeFinishedEvt:
          self.onDecodeFinished(objData)
          break
        case kRequestDataEvt:
          self.onRequestData(objData.o, objData.a)
          break
        case kSeekToRsp:
          self.onSeekToRsp(objData.r)
          break
      }
    }
  }

  createNewLogger(module) {
    return new Logger(module)
  }

  play(url, canvas, callback, waitHeaderLength, isStream = true, hasAudio = false) {
    this.logger.logInfo(`Play ${url}.`)

    let ret = {
      e: 0,
      m: 'Success'
    }

    do {
      if (this.playerState === playerStatePausing) {
        ret = this.resume()
        break
      }

      if (this.playerState === playerStatePlaying) {
        break
      }

      if (!url) {
        ret = {
          e: -1,
          m: 'Invalid url'
        }
        this.logger.logError('[ER] playVideo error, url empty.')
        break
      }

      if (!canvas) {
        ret = {
          e: -2,
          m: 'Canvas not set'
        }
        this.logger.logError('[ER] playVideo error, canvas empty.')
        break
      }

      if (!this.downloadWorker) {
        ret = {
          e: -3,
          m: 'Downloader not initialized'
        }
        this.logger.logError('[ER] Downloader not initialized.')
        break
      }

      if (!this.decodeWorker) {
        ret = {
          e: -4,
          m: 'Decoder not initialized'
        }
        this.logger.logError('[ER] Decoder not initialized.')
        break
      }

      if (url.startsWith('ws://') || url.startsWith('wss://')) {
        this.downloadProto = kProtoWebsocket
      } else {
        this.downloadProto = kProtoHttp
      }

      this.fileInfo = new FileInfo(url)
      this.canvas = canvas
      this.callback = callback
      this.waitHeaderLength = waitHeaderLength || this.waitHeaderLength
      this.playerState = playerStatePlaying
      this.isStream = isStream
      this.hasAudio = hasAudio
      this.startTrackTimer()
      this.displayLoop()

      // var playCanvasContext = playCanvas.getContext("2d"); //If get 2d, webgl will be disabled.
      this.webglPlayer = new WebGLPlayer(this.canvas, {
        preserveDrawingBuffer: false
      })

      if (!this.isStream) {
        const req = {
          t: kGetFileInfoReq,
          u: url,
          p: this.downloadProto
        }
        this.downloadWorker.postMessage(req)
      } else {
        this.requestStream(url)
        this.onGetFileInfo({
          sz: -1,
          st: 200
        })
      }

      const self = this
      this.registerVisibilityEvent((visible) => {
        if (visible) {
          self.resume()
        } else {
          self.pause()
        }
      })

      this.buffering = true
      this.showLoading()
    } while (false)

    return ret
  }

  pauseStream() {
    let ret = {}
    if (this.playerState !== playerStatePlaying) {
      ret = {
        e: -1,
        m: 'Not playing'
      }
      return ret
    }

    this.streamPauseParam = {
      url: this.fileInfo.url,
      canvas: this.canvas,
      callback: this.callback,
      waitHeaderLength: this.waitHeaderLength
    }

    this.logger.logInfo('Stop in stream pause.')
    this.stop()

    ret = {
      e: 0,
      m: 'Success'
    }

    return ret
  }

  pause() {
    if (this.isStream) {
      return this.pauseStream()
    }

    this.logger.logInfo('Pause.')

    let ret = {}
    if (this.playerState !== playerStatePlaying) {
      ret = {
        e: -1,
        m: 'Not playing'
      }
      return ret
    }

    // Pause video rendering and audio flushing.
    this.playerState = playerStatePausing

    // Pause audio context.
    if (this.pcmPlayer) {
      this.pcmPlayer.pause()
    }

    // Pause decoding.
    this.pauseDecoding()

    // Stop track timer.
    this.stopTrackTimer()

    // Do not stop downloader for background buffering.
    ret = {
      e: 0,
      m: 'Success'
    }

    return ret
  }

  resumeStream() {
    let ret = {}

    if (this.playerState !== playerStateIdle || !this.streamPauseParam) {
      ret = {
        e: -1,
        m: 'Not pausing'
      }
      return ret
    }

    this.logger.logInfo('Play in stream resume.')
    this.play(
      this.streamPauseParam.url,
      this.streamPauseParam.canvas,
      this.streamPauseParam.callback,
      this.streamPauseParam.waitHeaderLength,
      true
    )
    this.streamPauseParam = null

    ret = {
      e: 0,
      m: 'Success'
    }

    return ret
  }

  resume(fromSeek) {
    if (this.isStream) {
      return this.resumeStream()
    }

    this.logger.logInfo('Resume.')

    let ret = {}

    if (this.playerState !== playerStatePausing) {
      ret = {
        e: -1,
        m: 'Not pausing'
      }
      return ret
    }

    if (!fromSeek) {
      // Resume audio context.
      this.pcmPlayer.resume()
    }

    // If there's a flying video renderer op, interrupt it.
    if (this.videoRendererTimer != null) {
      clearTimeout(this.videoRendererTimer)
      this.videoRendererTimer = null
    }

    // Restart video rendering and audio flushing.
    this.playerState = playerStatePlaying

    // Restart decoding.
    this.startDecoding()

    // Restart track timer.
    if (!this.seeking) {
      this.startTrackTimer()
    }

    ret = {
      e: 0,
      m: 'Success'
    }
    return ret
  }

  stop() {
    let ret = {}

    this.logger.logInfo('Stop.')
    if (this.playerState === playerStateIdle) {
      ret = {
        e: -1,
        m: 'Not playing'
      }
      return ret
    }

    if (this.videoRendererTimer != null) {
      clearTimeout(this.videoRendererTimer)
      this.videoRendererTimer = null
      this.logger.logInfo('Video renderer timer stopped.')
    }

    this.stopDownloadTimer()
    this.stopTrackTimer()
    this.hideLoading()

    this.fileInfo = null
    this.canvas = null
    this.webglPlayer = null
    this.callback = null
    this.duration = 0
    this.pixFmt = 0
    this.videoWidth = 0
    this.videoHeight = 0
    this.yLength = 0
    this.uvLength = 0
    this.beginTimeOffset = 0
    this.decoderState = decoderStateIdle
    this.playerState = playerStateIdle
    this.decoding = false
    this.frameBuffer = []
    this.buffering = false
    this.streamReceivedLen = 0
    this.firstFrame = true
    this.urgent = false
    this.seekReceivedLen = 0

    if (this.pcmPlayer) {
      this.pcmPlayer.destroy()
      this.pcmPlayer = null
      this.logger.logInfo('Pcm player released.')
    }

    if (this.timeTrack) {
      this.timeTrack.value = 0
    }

    this.logger.logInfo('Closing decoder.')
    this.decodeWorker.postMessage({
      t: kCloseDecoderReq
    })

    this.logger.logInfo('Uniniting decoder.')
    this.decodeWorker.postMessage({
      t: kUninitDecoderReq
    })

    if (this.fetchController) {
      this.fetchController.abort()
      this.fetchController = null
    }

    ret = {
      e: 0,
      m: 'success'
    }

    return ret
  }

  seekTo(ms) {
    if (this.isStream) {
      return
    }

    // Pause playing.
    this.pause()

    // Stop download.
    this.stopDownloadTimer()

    // Clear frame buffer.
    this.frameBuffer.length = 0

    // Request decoder to seek.
    this.decodeWorker.postMessage({
      t: kSeekToReq,
      ms
    })

    // Reset begin time offset.
    this.beginTimeOffset = ms / 1000
    this.logger.logInfo(`seekTo beginTimeOffset ${this.beginTimeOffset}`)

    this.seeking = true
    this.justSeeked = true
    this.urgent = true
    this.seekReceivedLen = 0
    this.startBuffering()
  }

  fullscreen() {
    if (this.webglPlayer) {
      this.webglPlayer.fullscreen()
    }
  }

  getState() {
    return this.playerState
  }

  isPlaying() {
    return this.playerState === playerStatePlaying
  }

  isPaused() {
    return (this.playerState = playerStatePausing)
  }

  setTrack(timeTrack, timeLabel) {
    this.timeTrack = timeTrack
    this.timeLabel = timeLabel

    if (this.timeTrack) {
      const self = this
      this.timeTrack.oninput = function () {
        if (!self.seeking) {
          self.seekTo(self.timeTrack.value)
        }
      }
      this.timeTrack.onchange = function () {
        if (!self.seeking) {
          self.seekTo(self.timeTrack.value)
        }
      }
    }
  }

  onGetFileInfo(info) {
    if (this.playerState === playerStateIdle) {
      return
    }

    this.logger.logInfo(`Got file size rsp:${info.st} size:${info.sz} byte.`)
    if (info.st === 200) {
      this.fileInfo.size = info.sz
      this.logger.logInfo('Initializing decoder.')
      const req = {
        t: kInitDecoderReq,
        s: this.fileInfo.size,
        l: this.waitHeaderLength,
        c: this.fileInfo.chunkSize
      }
      this.decodeWorker.postMessage(req)
    } else {
      this.reportPlayError(-1, info.st)
    }
  }

  onFileData(data, start, end, seq) {
    // this.logger.logInfo("Got data bytes=" + start + "-" + end + ".");
    this.downloading = false

    if (this.playerState === playerStateIdle) {
      return
    }

    if (seq !== this.downloadSeqNo) {
      return // Old data.
    }

    if (this.playerState === playerStatePausing) {
      if (this.seeking) {
        this.seekReceivedLen += data.byteLength
        const left = this.fileInfo.size - this.fileInfo.offset
        const seekWaitLen = Math.min(left, this.seekWaitLen)
        if (this.seekReceivedLen >= seekWaitLen) {
          this.logger.logInfo('Resume in seek now')
          setTimeout(() => {
            this.resume(true)
          }, 0)
        }
      } else {
        return
      }
    }

    const len = end - start + 1
    this.fileInfo.offset += len

    const objData = {
      t: kFeedDataReq,
      d: data
    }
    this.decodeWorker.postMessage(objData, [objData.d])

    switch (this.decoderState) {
      case decoderStateIdle:
        this.onFileDataUnderDecoderIdle()
        break
      case decoderStateInitializing:
        this.onFileDataUnderDecoderInitializing()
        break
      case decoderStateReady:
        this.onFileDataUnderDecoderReady()
        break
    }

    if (this.urgent) {
      setTimeout(() => {
        this.downloadOneChunk()
      }, 0)
    }
  }

  onFileDataUnderDecoderIdle() {
    if (this.fileInfo.offset >= this.waitHeaderLength) {
      this.logger.logInfo('Opening decoder.')
      this.decoderState = decoderStateInitializing
      const req = {
        t: kOpenDecoderReq
      }
      this.decodeWorker.postMessage(req)
    }

    this.downloadOneChunk()
  }

  onFileDataUnderDecoderInitializing() {
    this.downloadOneChunk()
  }

  onFileDataUnderDecoderReady() {
    // this.downloadOneChunk();
  }

  onInitDecoder(objData) {
    if (this.playerState === playerStateIdle) {
      return
    }

    this.logger.logInfo(`Init decoder response ${objData.e}.`)
    if (objData.e === 0) {
      if (!this.isStream) {
        this.downloadOneChunk()
      }
    } else {
      this.reportPlayError(objData.e)
    }
  }

  onOpenDecoder(objData) {
    if (this.playerState === playerStateIdle) {
      return
    }

    this.logger.logInfo(`Open decoder response ${objData.e}.`)
    if (objData.e === 0) {
      this.onVideoParam(objData.v)
      this.onAudioParam(objData.a)
      this.decoderState = decoderStateReady
      this.logger.logInfo('Decoder ready now.')
      this.startDecoding()
    } else {
      this.reportPlayError(objData.e)
    }
  }

  onVideoParam(v) {
    if (this.playerState === playerStateIdle) {
      return
    }

    this.logger.logInfo(
      `Video param duation:${v.d} pixFmt:${v.p} width:${v.w} height:${v.h}.`
    )
    this.duration = v.d
    this.pixFmt = v.p
    // this.canvas.width = v.w;
    // this.canvas.height = v.h;
    this.videoWidth = v.w
    this.videoHeight = v.h
    this.yLength = this.videoWidth * this.videoHeight
    this.uvLength = (this.videoWidth / 2) * (this.videoHeight / 2)

    /*
      //var playCanvasContext = playCanvas.getContext("2d"); //If get 2d, webgl will be disabled.
      this.webglPlayer = new WebGLPlayer(this.canvas, {
          preserveDrawingBuffer: false
      });
      */

    if (this.timeTrack) {
      this.timeTrack.min = 0
      this.timeTrack.max = this.duration
      this.timeTrack.value = 0
      this.displayDuration = this.formatTime(this.duration / 1000)
    }

    const byteRate = (1000 * this.fileInfo.size) / this.duration
    const targetSpeed = downloadSpeedByteRateCoef * byteRate
    const chunkPerSecond = targetSpeed / this.fileInfo.chunkSize
    this.chunkInterval = 1000 / chunkPerSecond
    this.seekWaitLen = byteRate * maxBufferTimeLength * 2
    this.logger.logInfo(`Seek wait len ${this.seekWaitLen}`)

    if (!this.isStream) {
      this.startDownloadTimer()
    }

    this.logger.logInfo(
      `Byte rate:${byteRate} target speed:${targetSpeed} chunk interval:${this.chunkInterval}.`
    )
  }

  onAudioParam(a) {
    if (this.playerState === playerStateIdle) {
      return
    }

    this.logger.logInfo(
      `Audio param sampleFmt:${a.f} channels:${a.c} sampleRate:${a.r}.`
    )

    const sampleFmt = a.f
    const channels = a.c
    const sampleRate = a.r

    let encoding = '16bitInt'
    switch (sampleFmt) {
      case 0:
        encoding = '8bitInt'
        break
      case 1:
        encoding = '16bitInt'
        break
      case 2:
        encoding = '32bitInt'
        break
      case 3:
        encoding = '32bitFloat'
        break
      default:
        this.logger.logError(`Unsupported audio sampleFmt ${sampleFmt}!`)
    }
    this.logger.logInfo(`Audio encoding ${encoding}.`)

    this.pcmPlayer = new PCMPlayer({
      encoding,
      channels,
      sampleRate,
      flushingTime: 5000
    })

    this.audioEncoding = encoding
    this.audioChannels = channels
    this.audioSampleRate = sampleRate
  }

  restartAudio() {
    if (this.pcmPlayer) {
      this.pcmPlayer.destroy()
      this.pcmPlayer = null
    }

    this.pcmPlayer = new PCMPlayer({
      encoding: this.audioEncoding,
      channels: this.audioChannels,
      sampleRate: this.audioSampleRate,
      flushingTime: 5000
    })
  }

  bufferFrame(frame) {
    // If not decoding, it may be frame before seeking, should be discarded.
    if (!this.decoding) {
      return
    }
    this.frameBuffer.push(frame)

    //  this.logger.logInfo('bufferFrame ' + frame.s + ', seq ' + frame.q)

    if (
      this.getBufferTimerLength() >= maxBufferTimeLength ||
      this.decoderState === decoderStateFinished
    ) {
      if (this.decoding) {
        this.logger.logInfo('Frame buffer time length >= ' + maxBufferTimeLength + ', pause decoding.')
        this.pauseDecoding()
      }
      if (this.buffering) {
        this.stopBuffering()
      }
    }
  }

  displayAudioFrame(frame) {
    if (this.playerState !== playerStatePlaying) {
      return false
    }

    if (this.seeking) {
      this.restartAudio()
      this.startTrackTimer()
      this.hideLoading()
      this.seeking = false
      this.urgent = false
    }

    if (this.isStream && this.firstFrame) {
      this.firstFrame = false
      this.beginTimeOffset = frame.s
    }

    this.pcmPlayer.play(new Uint8Array(frame.d))
    return true
  }

  onAudioFrame(frame) {
    this.bufferFrame(frame)
  }

  onDecodeFinished(objData) {
    this.pauseDecoding()
    this.decoderState = decoderStateFinished
  }

  getBufferTimerLength() {
    if (!this.frameBuffer || this.frameBuffer.length === 0) {
      return 0
    }

    const oldest = this.frameBuffer[0]
    const newest = this.frameBuffer[this.frameBuffer.length - 1]
    return newest.s - oldest.s
  }

  onVideoFrame(frame) {
    this.bufferFrame(frame)
  }

  displayVideoFrame(frame) {
    if (this.playerState !== playerStatePlaying) {
      this.logger.logInfo('player status is ' + this.playerState)
      return false
    }

    if (this.seeking) {
      this.restartAudio()
      this.startTrackTimer()
      this.hideLoading()
      this.seeking = false
      this.urgent = false
    }

    if (!this.hasAudio && this.isStream && this.firstFrame) {
      this.firstFrame = false
      this.beginTimeOffset = frame.s - this.pcmPlayer.getTimestamp() + maxBufferTimeLength / 2
    }

    const audioCurTs = this.pcmPlayer.getTimestamp()
    const audioTimestamp = audioCurTs + this.beginTimeOffset
    const delay = frame.s - audioTimestamp

    if (audioTimestamp <= 0 || delay <= 0) {
      const data = new Uint8Array(frame.d)
      this.renderVideoFrame(data)
      return true
    }
  }

  onSeekToRsp(ret) {
    if (ret !== 0) {
      this.justSeeked = false
      this.seeking = false
    }
  }

  onRequestData(offset, available) {
    if (this.justSeeked) {
      this.logger.logInfo(`Request data ${offset}, available ${available}`)
      if (offset === -1) {
        // Hit in buffer.
        const left = this.fileInfo.size - this.fileInfo.offset
        if (available >= left) {
          this.logger.logInfo('No need to wait')
          this.resume()
        } else {
          this.startDownloadTimer()
        }
      } else {
        if (offset >= 0 && offset < this.fileInfo.size) {
          this.fileInfo.offset = offset
        }
        this.startDownloadTimer()
      }

      // this.restartAudio();
      this.justSeeked = false
    }
  }

  displayLoop() {
    requestAnimationFrame(this.displayLoop.bind(this))
    if (this.playerState !== playerStatePlaying) {
      return
    }

    if (this.frameBuffer.length === 0) {
      return
    }

    if (this.buffering) {
      return
    }

    // requestAnimationFrame may be 60fps, if stream fps too large,
    // we need to render more frames in one loop, otherwise display
    // fps won't catch up with source fps, leads to memory increasing,
    // set to 2 now.
    for (let i = 0; i < 2; ++i) {
      const frame = this.frameBuffer[0]
      switch (frame.t) {
        case kAudioFrame:
          if (this.displayAudioFrame(frame)) {
            this.frameBuffer.shift()
          }
          break
        case kVideoFrame:
          if (this.displayVideoFrame(frame)) {
            this.frameBuffer.shift()
          }
          break
        default:
          return
      }

      if (this.frameBuffer.length === 0) {
        break
      }
    }

    if (this.getBufferTimerLength() < maxBufferTimeLength / 2) {
      if (!this.decoding) {
        // this.logger.logInfo('Buffer time length < ' + maxBufferTimeLength / 2 + ', restart decoding.')
        this.startDecoding()
      }
    }

    if (this.bufferFrame.length === 0) {
      if (this.decoderState === decoderStateFinished) {
        this.reportPlayError(1, 0, 'Finished')
        this.stop()
      } else {
        this.startBuffering()
      }
    }
  }

  startBuffering() {
    this.buffering = true
    this.showLoading()
    this.pause()
  }

  stopBuffering() {
    this.buffering = false
    this.hideLoading()
    this.resume()
  }

  renderVideoFrame(data) {
    this.webglPlayer.renderFrame(
      data,
      this.videoWidth,
      this.videoHeight,
      this.yLength,
      this.uvLength
    )
  }

  downloadOneChunk() {
    if (this.downloading || this.isStream) {
      return
    }

    const start = this.fileInfo.offset
    if (start >= this.fileInfo.size) {
      this.logger.logError('Reach file end.')
      this.stopDownloadTimer()
      return
    }

    let end = this.fileInfo.offset + this.fileInfo.chunkSize - 1
    if (end >= this.fileInfo.size) {
      end = this.fileInfo.size - 1
    }

    const len = end - start + 1
    if (len > this.fileInfo.chunkSize) {
      this.logger.logInfo(
        `Error: request len:${len} > chunkSize:${this.fileInfo.chunkSize}`
      )
      return
    }

    const req = {
      t: kDownloadFileReq,
      u: this.fileInfo.url,
      s: start,
      e: end,
      q: this.downloadSeqNo,
      p: this.downloadProto
    }
    this.downloadWorker.postMessage(req)
    this.downloading = true
  }

  startDownloadTimer() {
    const self = this
    this.downloadSeqNo++
    this.downloadTimer = setInterval(() => {
      self.downloadOneChunk()
    }, this.chunkInterval)
  }

  stopDownloadTimer() {
    if (this.downloadTimer != null) {
      clearInterval(this.downloadTimer)
      this.downloadTimer = null
    }
    this.downloading = false
  }

  startTrackTimer() {
    const self = this
    this.trackTimer = setInterval(() => {
      self.updateTrackTime()
    }, this.trackTimerInterval)
  }

  stopTrackTimer() {
    if (this.trackTimer != null) {
      clearInterval(this.trackTimer)
      this.trackTimer = null
    }
  }

  updateTrackTime() {
    if (this.playerState === playerStatePlaying && this.pcmPlayer) {
      const currentPlayTime =
        this.pcmPlayer.getTimestamp() + this.beginTimeOffset
      if (this.timeTrack) {
        this.timeTrack.value = 1000 * currentPlayTime
      }

      if (this.timeLabel) {
        this.timeLabel.innerHTML = `${this.formatTime(currentPlayTime)}/${
          this.displayDuration
        }`
      }
    }
  }

  startDecoding() {
    const req = {
      t: kStartDecodingReq,
      i: this.urgent ? 0 : this.decodeInterval
    }
    this.decodeWorker.postMessage(req)
    this.decoding = true
  }

  discardData() {
    const req = {
      t: KDiscardDataReq
    }
    this.decodeWorker.postMessage(req)
  }

  pauseDecoding() {
    const req = {
      t: kPauseDecodingReq
    }
    this.decodeWorker.postMessage(req)
    this.decoding = false
  }

  formatTime(s) {
    const h =
      Math.floor(s / 3600) < 10
        ? `0${Math.floor(s / 3600)}`
        : Math.floor(s / 3600)
    const m =
      Math.floor((s / 60) % 60) < 10
        ? `0${Math.floor((s / 60) % 60)}`
        : Math.floor((s / 60) % 60)

    const s1 = Math.floor(s % 60) < 10 ? `0${Math.floor(s % 60)}` : Math.floor(s % 60)
    return (`${h}:${m}:${s1}`)
  }

  reportPlayError(error, status, message) {
    const e = {
      error: error || 0,
      status: status || 0,
      message
    }

    if (this.callback) {
      this.callback(e)
    }
  }

  setLoadingDiv(loadingDiv) {
    this.loadingDiv = loadingDiv
  }

  hideLoading() {
    if (this.loadingDiv != null) {
      this.loadingDiv.style.display = 'none'
    }
  }

  showLoading() {
    if (this.loadingDiv != null) {
      this.loadingDiv.style.display = 'block'
    }
  }

  registerVisibilityEvent(cb) {
    let hidden = 'hidden'

    // Standards:
    if (hidden in document) {
      document.addEventListener('visibilitychange', onchange)
    } else if ((hidden = 'mozHidden') in document) {
      document.addEventListener('mozvisibilitychange', onchange)
    } else if ((hidden = 'webkitHidden') in document) {
      document.addEventListener('webkitvisibilitychange', onchange)
    } else if ((hidden = 'msHidden') in document) {
      document.addEventListener('msvisibilitychange', onchange)
    } else if ('onfocusin' in document) {
      // IE 9 and lower.
      document.onfocusin = document.onfocusout = onchange
    } else {
      // All others.
      window.onpageshow = window.onpagehide = window.onfocus = window.onblur = onchange
    }

    function onchange(evt) {
      const v = true
      const h = false
      const evtMap = {
        focus: v,
        focusin: v,
        pageshow: v,
        blur: h,
        focusout: h,
        pagehide: h
      }

      evt = evt || window.event
      let visible = v
      if (evt.type in evtMap) {
        visible = evtMap[evt.type]
      } else {
        visible = this[hidden] ? h : v
      }
      cb(visible)
    }

    // set the initial state (but only if browser supports the Page Visibility API)
    if (document[hidden] !== undefined) {
      onchange({ type: document[hidden] ? 'blur' : 'focus' })
    }
  }

  onStreamDataUnderDecoderIdle(length) {
    if (this.streamReceivedLen >= this.waitHeaderLength) {
      this.logger.logInfo('Opening decoder.')
      this.decoderState = decoderStateInitializing
      const req = {
        t: kOpenDecoderReq
      }
      this.decodeWorker.postMessage(req)
    } else {
      this.streamReceivedLen += length
    }
  }

  requestStream(url) {
    const self = this
    this.fetchController = new AbortController()
    const { signal } = this.fetchController

    fetch(url, { signal })
      .then(async (response) => {
        const reader = response.body.getReader()
        reader.read().then(function processData({ done, value }) {
          if (done) {
            self.logger.logInfo('Stream done.')
            return
          }

          if (self.playerState !== playerStatePlaying) {
            return
          }

          let dataLength = value.byteLength
          let offset = 0
          if (dataLength > self.fileInfo.chunkSize) {
            do {
              const len = Math.min(self.fileInfo.chunkSize, dataLength)
              const data = value.buffer.slice(offset, offset + len)
              dataLength -= len
              offset += len
              const objData = {
                t: kFeedDataReq,
                d: data
              }
              self.decodeWorker.postMessage(objData, [objData.d])
            } while (dataLength > 0)
          } else {
            const objData = {
              t: kFeedDataReq,
              d: value.buffer
            }
            self.decodeWorker.postMessage(objData, [objData.d])
          }

          if (self.decoderState === decoderStateIdle) {
            self.onStreamDataUnderDecoderIdle(dataLength)
          }

          return reader.read().then(processData)
        })
      })
      .catch((err) => {
        this.logger.logError(err)
      })
  }
}

window.Player = Player

export default Player
