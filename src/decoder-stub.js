
import Logger from './logger'
import ReconnectingWebSocket from 'reconnecting-websocket'

/// ----------------------------------------------------------------------------
class BaseRequest {
  constructor(cmd) {
    this.cmd = cmd
  }
}

// class BaseRespnse {
//   constructor(cmd) {
//     this.cmd = cmd
//     this.code = 0
//     this.msg = ''
//   }
// }

/// ----------------------------------------------------------------------------
class InitDecoderRequest extends BaseRequest {
  constructor(fileSize, waitHeaderLength) {
    super('initDecoder')
    this.fileSize = fileSize
    this.waitHeaderLength = waitHeaderLength
  }
}

/// ----------------------------------------------------------------------------
class UninitDecoderRequest extends BaseRequest {
  constructor(fileSize) {
    super('uninitDecoder')
    this.fileSize = fileSize
  }
}

/// ----------------------------------------------------------------------------
class OpenDecoderRequest extends BaseRequest {
  constructor(hasVideo, hasAudio) {
    super('openDecoder')
    this.hasVideo = hasVideo
    this.hasAudio = hasAudio
  }
}

/// ----------------------------------------------------------------------------
class CloseDecoderRequest extends BaseRequest {
  constructor() {
    super('closeDecoder')
  }
}

/// ----------------------------------------------------------------------------
class StartDecodeRequest extends BaseRequest {
  constructor() {
    super('startDecode')
  }
}

/// ----------------------------------------------------------------------------
class StopDecodeRequest extends BaseRequest {
  constructor() {
    super('stopDecode')
  }
}

/// ----------------------------------------------------------------------------
class RequestDataRequest extends BaseRequest {
  constructor(offset, available) {
    super('requestData')
    this.offset = offset
    this.available = available
  }
}

/// ----------------------------------------------------------------------------
class DecoderStub {
  constructor() {
    this.logger = new Logger('FFmpeg')

    // --callback
    this.onVideo = null
    this.onAudio = null
    this.onRequestData = null
    this.onInitDecoderSucceed = null
    this.onInitDecoderFailed = null
    this.onOpenDecoderSucceed = null
    this.onOpenDecoderFailed = null
    this.onStartDecodeSucceed = null
    this.onStartDecodeFailed = null
    this.onStopDecodeSucceed = null
    this.onStopDecodeFailed = null
    this.onSeekToSucceed = null
    this.onSeekToFailed = null

    // logger
    this.logger.logInfo('Init ffmpeg decoder')

    // websocket
    this.ws = new ReconnectingWebSocket('ws://localhost:9002/decode')
    this.ws.binaryType = 'arraybuffer'
    this.websocketOpened = false

    const self = this
    this.ws.onopen = function() {
      self.logger.logInfo('open websocket, start to init decoder')
      self.websocketOpened = true
    }
    this.ws.onclose = function(e) {
      self.logger.logInfo('close')
    }
    this.ws.onerror = function(e) {
      self.logger.logError(e)
    }
    this.ws.onmessage = function(e) {
      if (typeof (e.data) === 'string') {
        self.onTextMessage(e.data)
      } else {
        self.onBinaryMessage(e.data)
      }
    }
  }

  initDecoder(fileSize, waitHeaderLength, onInitDecoderSucceed, onInitDecoderFailed) {
    this.onInitDecoderSucceed = onInitDecoderSucceed
    this.onInitDecoderFailed = onInitDecoderFailed

    let count = 5
    while ((count--) > 0) {
      if (this.websocketOpened) {
        this.sendCommand(new InitDecoderRequest(fileSize, waitHeaderLength))
        break
      }
      this.sleep(100)
    }
  }

  onTextMessage(msg) {
    this.logger.logInfo(`Text message => ${msg}`)

    const data = JSON.parse(msg)

    switch (data.cmd) {
      case 'initDecoder': {
        if (data.code === 0) {
          if (this.onInitDecoderSucceed != null) {
            this.onInitDecoderSucceed(data)
          }
        } else {
          if (this.onInitDecoderFailed != null) {
            this.onInitDecoderFailed(data.code, data.msg)
          }
        }
        break
      }
      case 'openDecoder': {
        if (data.code === 0) {
          if (this.onOpenDecoderSucceed != null) {
            this.onOpenDecoderSucceed(data)
          }
        } else {
          if (this.onOpenDecoderFailed != null) {
            this.onOpenDecoderFailed(data.code, data.msg)
          }
        }
        break
      }
      case 'startDecode': {
        if (data.code === 0) {
          if (this.onStartDecodeSucceed != null) {
            this.onStartDecodeSucceed(data)
          }
        } else {
          if (this.onStartDecodeFailed != null) {
            this.onStartDecodeFailed(data.code, data.msg)
          }
        }
        break
      }
      case 'stopDecode': {
        if (data.code === 0) {
          if (this.onStopDecodeSucceed != null) {
            this.onStopDecodeSucceed(data)
          }
        } else {
          if (this.onStopDecodeFailed != null) {
            this.onStopDecodeFailed(data.code, data.msg)
          }
        }
        break
      }
      case 'requestData': {
        if (this.onRequestData != null) {
          this.onRequestData(data)
        }
        break
      }
      default: {
        break
      }
    }
  }

  onBinaryMessage(arrayBuffer) {
    const flagLen = 1
    const timestampLen = 16

    const flag = parseInt(new Uint8Array(arrayBuffer, 0, flagLen))
    const timestamp = parseFloat(String.fromCharCode.apply(null, new Uint8Array(arrayBuffer, flagLen, timestampLen + flagLen)))
    const dataArray = new Uint8Array(arrayBuffer, flagLen + timestampLen)

    if (flag === 0 && this.onVideo !== null) {
      this.onVideo(dataArray, timestamp)
    } else if (flag === 1 && this.onAudio != null) {
      this.onAudio(dataArray, timestamp)
    }
  }

  uninitDecoder() {
    this.sendCommand(new UninitDecoderRequest())
  }

  openDecoder(hasVideo, hasAudio, onOpenDecoderSucceed, onOpenDecoderFailed, onVideo, onAudio, onRequestData) {
    this.onOpenDecoderSucceed = onOpenDecoderSucceed
    this.onOpenDecoderFailed = onOpenDecoderFailed
    this.onVideo = onVideo
    this.onAudio = onAudio
    this.onRequestData = onRequestData

    this.sendCommand(new OpenDecoderRequest(hasVideo, hasAudio))
  }

  closeDecoder() {
    this.sendCommand(new CloseDecoderRequest())
  }

  sendData(data, len) {
    this.sendBinary(data)
  }

  stopDecode(onStopDecodeSucceed, onStopDecodeFailed) {
    this.onStopDecodeSucceed = onStopDecodeSucceed
    this.onStopDecodeFailed = onStopDecodeFailed
    this.sendCommand(new StopDecodeRequest())
  }

  startDecode(onStartDecodeSucceed, onStartDecodeFailed) {
    this.onStartDecodeSucceed = onStartDecodeSucceed
    this.onStartDecodeFailed = onStartDecodeFailed
    this.sendCommand(new StartDecodeRequest())
  }

  discardData() {
    this.sendCommand(new DiscardDataRequest())
  }

  seekTo(onSeekToSucceed, onSeekToFailed) {
    this.onSeekToSucceed = onSeekToSucceed
    this.onSeekToFailed = onSeekToFailed
  }

  sendCommand(cmdObj) {
    this.ws.send(JSON.stringify(cmdObj))
  }

  sendBinary(buffer) {
    this.ws.send(new Uint8Array(buffer).buffer)
  }

  sleep(time) {
    const startTime = new Date().getTime() + parseInt(time, 10)
    while (new Date().getTime() < startTime) {
      // --
    }
  };
}

export default DecoderStub
