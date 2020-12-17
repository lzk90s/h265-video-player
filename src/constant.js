export const { all } = require('promise-polyfill')

// Player request.
export const kPlayVideoReq = 0
export const kPauseVideoReq = 1
export const kStopVideoReq = 2

// Player response.
export const kPlayVideoRsp = 0
export const kAudioInfo = 1
export const kVideoInfo = 2
export const kAudioData = 3
export const kVideoData = 4

// Downloader request.
export const kGetFileInfoReq = 0
export const kDownloadFileReq = 1
export const kCloseDownloaderReq = 2

// Downloader response.
export const kGetFileInfoRsp = 0
export const kFileData = 1

// Downloader Protocol.
export const kProtoHttp = 0
export const kProtoWebsocket = 1

// Decoder request.
export const kInitDecoderReq = 0
export const kUninitDecoderReq = 1
export const kOpenDecoderReq = 2
export const kCloseDecoderReq = 3
export const kFeedDataReq = 4
export const kStartDecodingReq = 5
export const kPauseDecodingReq = 6
export const kSeekToReq = 7
export const KDiscardDataReq = 8

// Decoder response.
export const kInitDecoderRsp = 0
export const kUninitDecoderRsp = 1
export const kOpenDecoderRsp = 2
export const kCloseDecoderRsp = 3
export const kVideoFrame = 4
export const kAudioFrame = 5
export const kStartDecodingRsp = 6
export const kPauseDecodingRsp = 7
export const kDecodeFinishedEvt = 8
export const kRequestDataEvt = 9
export const kSeekToRsp = 10
