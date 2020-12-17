import Logger from './logger'
import DecoderStub from './decoder-stub'

import {
  kInitDecoderRsp, kCloseDecoderRsp, kRequestDataEvt,
  kAudioFrame, kVideoFrame, kSeekToRsp, kDecodeFinishedEvt,
  kInitDecoderReq, kUninitDecoderReq, kOpenDecoderReq, kCloseDecoderReq,
  kStartDecodingReq, kPauseDecodingReq, kFeedDataReq, kSeekToReq,
  kOpenDecoderRsp
} from './constant'

class Decoder {
  constructor() {
    this.logger = new Logger('Decoder')
    this.coreLogLevel = 1
    this.accurateSeek = true
    this.loaded = false
    this.tmpReqQue = []
    this.cacheBuffer = null
    this.decodeTimer = null
    this.videoCallback = null
    this.audioCallback = null
    this.requestCallback = null
    this.ffmpegStub = new DecoderStub()

    this.load()
  }

  initDecoder(fileSize, waitHeaderLength, chunkSize) {
    const ret = this.ffmpegStub.initDecoder(fileSize, waitHeaderLength,
      (data) => {
        self.logger.logInfo('initDecoder succeed')
      },
      (code, msg) => {
        self.logger.logError(`Failed to init decoder, code ${code}, msg ${msg}`)
        const objData = {
          t: kInitDecoderRsp,
          e: ret
        }
        self.postMessage(objData)
      })
  }

  uninitDecoder() {
    this.ffmpegStub.uninitDecoder(
      (data) => {},
      (code, msg) => {}
    )
  }

  openDecoder() {
    this.ffmpegStub.openDecoder(
      true,
      false,
      (data) => {
        const objData = {
          t: kOpenDecoderRsp,
          e: 0,
          v: {
            d: data.duration,
            p: data.videoPixFmt,
            w: data.videoWidth,
            h: data.videoHeight
          },
          a: {
            f: data.audioSampleFmt,
            c: data.audioChannels,
            r: data.audioSampleRate
          }
        }
        self.postMessage(objData)
      },
      (code, msg) => {
        const objData = {
          t: kOpenDecoderRsp,
          e: code
        }
        self.postMessage(objData)
      },
      (data, timestamp) => {
        const objData = {
          t: kVideoFrame,
          s: timestamp,
          d: data
        }
        self.postMessage(objData, [objData.d.buffer])
      },
      (data, timestamp) => {
        const objData = {
          t: kAudioFrame,
          s: timestamp,
          d: data
        }
        self.postMessage(objData, [objData.d.buffer])
      },
      (data) => {
        const objData = {
          t: kRequestDataEvt,
          o: data.offset,
          a: data.availble
        }
        self.postMessage(objData)
      }
    )
  }

  closeDecoder() {
    this.logger.logInfo('closeDecoder.')

    this.ffmpegStub.closeDecoder(
      (data) => {
        const objData = {
          t: kCloseDecoderRsp,
          e: 0
        }
        self.postMessage(objData)
      },
      (code, msg) => {
        const objData = {
          t: kCloseDecoderRsp,
          e: code
        }
        self.postMessage(objData)
      }
    )
  }

  startDecoding(interval) {
    this.logger.logInfo('start decoding')

    this.ffmpegStub.startDecode(
      (data) => {},
      (code, msg) => {
        if (code === 7) {
          self.decoder.logger.logInfo('Decoder finished.')
          const objData = {
            t: kDecodeFinishedEvt
          }
          self.postMessage(objData)
        } else if (code === 9) {
          self.logger.logInfo('Old frame')
          // self.ffmpegStub.startDecode(null, null)
        }
      }
    )
  }

  pauseDecoding() {
    this.logger.logInfo('Pause decoding.')
    this.ffmpegStub.stopDecode(
      (data) => {},
      (code, msg) => {}
    )
  }

  sendData(data) {
    this.ffmpegStub.sendData(data, data.length)
  }

  seekTo(ms) {
    const accurateSeek = this.accurateSeek ? 1 : 0
    const ret = this.ffmpegStub.seekTo(ms, accurateSeek)
    const objData = {
      t: kSeekToRsp,
      r: ret
    }
    self.postMessage(objData)
  }

  processReq(req) {
    // this.logger.logInfo('processReq ' + req.t + '.')
    switch (req.t) {
      case kInitDecoderReq:
        this.initDecoder(req.s, req.l, req.c)
        break
      case kUninitDecoderReq:
        this.uninitDecoder()
        break
      case kOpenDecoderReq:
        this.openDecoder()
        break
      case kCloseDecoderReq:
        this.closeDecoder()
        break
      case kStartDecodingReq:
        this.startDecoding(req.i)
        break
      case kPauseDecodingReq:
        this.pauseDecoding()
        break
      case kFeedDataReq:
        this.sendData(req.d)
        break
      case kSeekToReq:
        this.seekTo(req.ms)
        break
      default:
        this.logger.logError(`Unsupport messsage ${req.t}`)
    }
  }

  cacheReq(req) {
    if (req) {
      this.tmpReqQue.push(req)
    }
  }

  load() {
    this.logger.logInfo('on loaded.')
    this.loaded = true
    while (this.tmpReqQue.length > 0) {
      const req = this.tmpReqQue.shift()
      this.processReq(req)
    }
  }
}

self.decoder = new Decoder()

self.onmessage = function(evt) {
  if (!self.decoder) {
    console.log('[ER] Decoder not initialized!')
    return
  }

  const req = evt.data
  if (!self.decoder.loaded) {
    self.decoder.cacheReq(req)
    // self.decoder.logger.logInfo(`Temp cache req ${req.t}.`)
    return
  }

  self.decoder.processReq(req)
}

export default Decoder
