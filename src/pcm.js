import {} from './constant'

class PCMPlayer {
  constructor(option) {
    this.init(option)
  }

  init(option) {
    const defaults = {
      encoding: '16bitInt',
      channels: 1,
      sampleRate: 8000,
      flushingTime: 1000
    }
    this.option = Object.assign({}, defaults, option)
    this.samples = new Float32Array()
    this.flush = this.flush.bind(this)
    this.interval = setInterval(this.flush, this.option.flushingTime)
    this.maxValue = this.getMaxValue()
    this.typedArray = this.getTypedArray()
    this.createContext()
  }

  getMaxValue() {
    const encodings = {
      '8bitInt': 128,
      '16bitInt': 32768,
      '32bitInt': 2147483648,
      '32bitFloat': 1
    }

    return encodings[this.option.encoding]
      ? encodings[this.option.encoding]
      : encodings['16bitInt']
  }

  getTypedArray() {
    const typedArrays = {
      '8bitInt': Int8Array,
      '16bitInt': Int16Array,
      '32bitInt': Int32Array,
      '32bitFloat': Float32Array
    }

    return typedArrays[this.option.encoding]
      ? typedArrays[this.option.encoding]
      : typedArrays['16bitInt']
  }

  createContext() {
    this.audioCtx = new (window.AudioContext || window.webkitAudioContext)()
    this.gainNode = this.audioCtx.createGain()
    this.gainNode.gain.value = 1
    this.gainNode.connect(this.audioCtx.destination)
    this.startTime = this.audioCtx.currentTime
  }

  isTypedArray(data) {
    return (
      data.byteLength && data.buffer && data.buffer.constructor == ArrayBuffer
    )
  }

  feed(data) {
    if (!this.isTypedArray(data)) return
    data = this.getFormatedValue(data)
    const tmp = new Float32Array(this.samples.length + data.length)
    tmp.set(this.samples, 0)
    tmp.set(data, this.samples.length)
    this.samples = tmp
  }

  getFormatedValue(data) {
    const data1 = new this.typedArray(data.buffer)
    const float32 = new Float32Array(data.length)
    let i

    for (i = 0; i < data.length; i++) {
      float32[i] = data[i] / this.maxValue
    }
    return float32
  }

  volume(volume) {
    this.gainNode.gain.value = volume
  }

  destroy() {
    if (this.interval) {
      clearInterval(this.interval)
    }
    this.samples = null
    this.audioCtx.close()
    this.audioCtx = null
  }

  flush() {
    if (!this.samples.length) return
    const bufferSource = this.audioCtx.createBufferSource()
    const length = this.samples.length / this.option.channels
    const audioBuffer = this.audioCtx.createBuffer(
      this.option.channels,
      length,
      this.option.sampleRate
    )
    let audioData
    let channel
    let offset
    let i
    let decrement

    for (channel = 0; channel < this.option.channels; channel++) {
      audioData = audioBuffer.getChannelData(channel)
      offset = channel
      decrement = 50
      for (i = 0; i < length; i++) {
        audioData[i] = this.samples[offset]
        /* fadein */
        if (i < 50) {
          audioData[i] = (audioData[i] * i) / 50
        }
        /* fadeout */
        if (i >= length - 51) {
          audioData[i] = (audioData[i] * decrement--) / 50
        }
        offset += this.option.channels
      }
    }

    if (this.startTime < this.audioCtx.currentTime) {
      this.startTime = this.audioCtx.currentTime
    }
    // console.log('start vs current '+this.startTime+' vs '+this.audioCtx.currentTime+' duration: '+audioBuffer.duration);
    bufferSource.buffer = audioBuffer
    bufferSource.connect(this.gainNode)
    bufferSource.start(this.startTime)
    this.startTime += audioBuffer.duration
    this.samples = new Float32Array()
  }

  getTimestamp() {
    if (this.audioCtx) {
      return this.audioCtx.currentTime
    } else {
      return 0
    }
  }

  play(data) {
    if (!this.isTypedArray(data)) {
      return
    }

    data = this.getFormatedValue(data)
    if (!data.length) {
      return
    }

    const bufferSource = this.audioCtx.createBufferSource()
    const length = data.length / this.option.channels
    const audioBuffer = this.audioCtx.createBuffer(
      this.option.channels,
      length,
      this.option.sampleRate
    )
    let audioData
    let channel
    let offset
    let i
    let decrement

    for (channel = 0; channel < this.option.channels; channel++) {
      audioData = audioBuffer.getChannelData(channel)
      offset = channel
      decrement = 50
      for (i = 0; i < length; i++) {
        audioData[i] = data[offset]
        /* fadein */
        if (i < 50) {
          audioData[i] = (audioData[i] * i) / 50
        }
        /* fadeout */
        if (i >= length - 51) {
          audioData[i] = (audioData[i] * decrement--) / 50
        }
        offset += this.option.channels
      }
    }

    if (this.startTime < this.audioCtx.currentTime) {
      this.startTime = this.audioCtx.currentTime
    }
    // console.log('start vs current '+this.startTime+' vs '+this.audioCtx.currentTime+' duration: '+audioBuffer.duration);
    bufferSource.buffer = audioBuffer
    bufferSource.connect(this.gainNode)
    bufferSource.start(this.startTime)
    this.startTime += audioBuffer.duration
  }

  pause() {
    if (this.audioCtx.state === 'running') {
      this.audioCtx.suspend()
    }
  }

  resume() {
    if (this.audioCtx.state === 'suspended') {
      this.audioCtx.resume()
    }
  }
}

export default PCMPlayer
