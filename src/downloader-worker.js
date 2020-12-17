import Logger from './logger'
import {
  kGetFileInfoReq, kGetFileInfoRsp, kProtoHttp, kDownloadFileReq,
  kCloseDownloaderReq, kProtoWebsocket, kFileData
} from './constant'

class Downloader {
  constructor() {
    this.logger = new Logger('Downloader')
    this.ws = null
  }

  appendBuffer(buffer1, buffer2) {
    const tmp = new Uint8Array(buffer1.byteLength + buffer2.byteLength)
    tmp.set(new Uint8Array(buffer1), 0)
    tmp.set(new Uint8Array(buffer2), buffer1.byteLength)
    return tmp.buffer
  }

  reportFileSize(sz, st) {
    const objData = {
      t: kGetFileInfoRsp,
      i: {
        sz: sz,
        st: st
      }
    }

    // this.logger.logInfo("File size " + sz + " bytes.");
    self.postMessage(objData)
  }

  reportData(start, end, seq, data) {
    const objData = {
      t: kFileData,
      s: start,
      e: end,
      d: data,
      q: seq
    }
    self.postMessage(objData, [objData.d])
  }

  // Http implement.
  getFileInfoByHttp(url) {
    this.logger.logInfo('Getting file size ' + url + '.')
    let size = 0
    let status = 0
    let reported = false

    const xhr = new XMLHttpRequest()
    xhr.open('get', url, true)
    const self = this
    xhr.onreadystatechange = () => {
      const len = xhr.getResponseHeader('Content-Length')
      if (len) {
        size = len
      }

      if (xhr.status) {
        status = xhr.status
      }

      // Completed.
      if (!reported && ((size > 0 && status > 0) || xhr.readyState === 4)) {
        self.reportFileSize(size, status)
        reported = true
        xhr.abort()
      }
    }
    xhr.send()
  }

  downloadFileByHttp(url, start, end, seq) {
    // this.logger.logInfo("Downloading file " + url + ", bytes=" + start + "-" + end + ".");
    const xhr = new XMLHttpRequest()
    xhr.open('get', url, true)
    xhr.responseType = 'arraybuffer'
    xhr.setRequestHeader('Range', 'bytes=' + start + '-' + end)
    const self = this
    xhr.onload = function () {
      self.reportData(start, end, seq, xhr.response)
    }
    xhr.send()
  }

  // Websocket implement, NOTICE MUST call requestWebsocket serially, MUST wait
  // for result of last websocket request(cb called) for there's only one stream
  // exists.
  requestWebsocket(url, msg, cb) {
    if (this.ws == null) {
      this.ws = new WebSocket(url)
      this.ws.binaryType = 'arraybuffer'

      const self = this
      this.ws.onopen = function (evt) {
        self.logger.logInfo('Ws connected.')
        self.ws.send(msg)
      }

      this.ws.onerror = function (evt) {
        self.logger.logError('Ws connect error ' + evt.data)
      }

      this.ws.onmessage = cb.onmessage
    } else {
      this.ws.onmessage = cb.onmessage
      this.ws.send(msg)
    }
  }

  getFileInfoByWebsocket(url) {
    // this.logger.logInfo("Getting file size " + url + ".");

    // TBD, consider tcp sticky package.
    let data = null
    const expectLength = 4
    const self = this
    const cmd = {
      url: url,
      cmd: 'size'
    }
    this.requestWebsocket(url, JSON.stringify(cmd), {
      onmessage: function (evt) {
        if (data != null) {
          data = self.appendBuffer(data, evt.data)
        } else if (evt.data.byteLength < expectLength) {
          data = evt.data.slice(0)
        } else {
          data = evt.data
        }

        // Assume 4 bytes header as file size.
        if (data.byteLength === expectLength) {
          const int32array = new Int32Array(data, 0, 1)
          const size = int32array[0]
          self.reportFileSize(size, 200)
          // self.logger.logInfo("Got file size " + self.fileSize + ".");
        }
      }
    })
  }

  downloadFileByWebsocket(url, start, end, seq) {
    // this.logger.logInfo("Downloading file " + url + ", bytes=" + start + "-" + end + ".");
    let data = null
    const expectLength = end - start + 1
    const self = this
    const cmd = {
      url: url,
      cmd: 'data',
      start: start,
      end: end
    }
    this.requestWebsocket(url, JSON.stringify(cmd), {
      onmessage: function (evt) {
        if (data != null) {
          data = self.appendBuffer(data, evt.data)
        } else if (evt.data.byteLength < expectLength) {
          data = evt.data.slice(0)
        } else {
          data = evt.data
        }

        // Wait for expect data length.
        if (data.byteLength === expectLength) {
          self.reportData(start, end, seq, data)
        }
      }
    })
  }

  // Interface.
  getFileInfo(proto, url) {
    switch (proto) {
      case kProtoHttp:
        this.getFileInfoByHttp(url)
        break
      case kProtoWebsocket:
        this.getFileInfoByWebsocket(url)
        break
      default:
        this.logger.logError('Invalid protocol ' + proto)
        break
    }
  }

  downloadFile(proto, url, start, end, seq) {
    switch (proto) {
      case kProtoHttp:
        this.downloadFileByHttp(url, start, end, seq)
        break
      case kProtoWebsocket:
        this.downloadFileByWebsocket(url, start, end, seq)
        break
      default:
        this.logger.logError('Invalid protocol ' + proto)
        break
    }
  }
}

self.downloader = new Downloader()

self.onmessage = function(evt) {
  if (!self.downloader) {
    console.log('[ER] Downloader not initialized!')
    return
  }

  const objData = evt.data
  switch (objData.t) {
    case kGetFileInfoReq:
      self.downloader.getFileInfo(objData.p, objData.u)
      break
    case kDownloadFileReq:
      self.downloader.downloadFile(
        objData.p,
        objData.u,
        objData.s,
        objData.e,
        objData.q
      )
      break
    case kCloseDownloaderReq:
      // Nothing to do.
      break
    default:
      self.downloader.logger.logError('Unsupport messsage ' + objData.t)
  }
}

export default Downloader
