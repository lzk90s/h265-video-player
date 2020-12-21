#include <exception>
#include <memory>
#include <mutex>

#include "common/helper/raii.h"
#include "common/helper/logger.h"
#include "common/helper/singleton.h"
#include "common/helper/threadpool.h"
#include "common/helper/timer.h"
#include "server/pojo.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"
#ifdef __cplusplus
}
#endif

namespace decoder {

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

typedef enum ErrorCode {
    kErrorCode_Success = 0,
    kErrorCode_Invalid_Param,
    kErrorCode_Invalid_State,
    kErrorCode_Invalid_Data,
    kErrorCode_Invalid_Format,
    kErrorCode_NULL_Pointer,
    kErrorCode_Open_File_Error,
    kErrorCode_Eof,
    kErrorCode_FFmpeg_Error,
    kErrorCode_Old_Frame
} ErrorCode;

class FFmpegLibrary {
public:
    typedef enum LogLevel {
        kLogLevel_None, // Not logging.
        kLogLevel_Core, // Only logging core module(without ffmpeg).
        kLogLevel_All   // Logging all, with ffmpeg.
    } LogLevel;

public:
    FFmpegLibrary() : logLevel_(kLogLevel_Core) {
        // av_register_all()' is deprecated
        // av_register_all();
        // avcodec_register_all();
        av_log_set_level(AV_LOG_TRACE);
        av_log_set_callback(FFmpegLibrary::ffmpegLogCallback);
    }

    void setLogLevel(int32_t level) { this->logLevel_ = level; }

    int32_t getLogLevel() { return this->logLevel_; }

    ~FFmpegLibrary() { av_log_set_callback(nullptr); }

    static void ffmpegLogCallback(void *ptr, int32_t level, const char *fmt, va_list vl) {
        static int32_t printPrefix = 1;
        char line[1024]            = {0};
        AVClass *avc               = ptr ? *(AVClass **)ptr : nullptr;

        if (level > AV_LOG_DEBUG || INSTANCE().logLevel_ <= kLogLevel_Core) {
            return;
        }

        line[0] = 0;

        if (printPrefix && avc) {
            if (avc->parent_log_context_offset) {
                AVClass **parent = *(AVClass ***)(((uint8_t *)ptr) + avc->parent_log_context_offset);
                if (parent && *parent) {
                    snprintf(line, sizeof(line), "[%s @ %p] ", (*parent)->item_name(parent), parent);
                }
            }
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ", avc->item_name(ptr), ptr);
        }

        vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);
        line[strlen(line) + 1] = 0;

        LOG_INFO("[ffmpeg] {}", line);
    }

    const static FFmpegLibrary &INSTANCE() { return common::Singleton<FFmpegLibrary>::getInstance(); }

private:
    int32_t logLevel_;
};

class FFmpegWrapper {
public:
    const int32_t kCustomIoBufferSize    = 32 * 1024;
    const int32_t kInitialPcmBufferSize  = 128 * 1024;
    const int32_t kDefaultFifoSize       = 8 * 1024 * 1024;
    const int32_t kMaxFifoSize           = 16 * 1024 * 1024;
    const int32_t KDecodeTimerInterval   = 5;
    const int32_t KTimeStampStrLength    = 16; // 时间戳字符串长度，需要和js部分约定一样
    const int32_t KDecodedDataTypeLength = 1;  // 数据类型长度，需要和js部分约定一样
    const int8_t KVideoFrameFlag         = 0;
    const int8_t KAudioFrameFlag         = 1;

    using onVideo       = std::function<void(uint8_t *buff, int32_t size)>;
    using onAudio       = std::function<void(uint8_t *buff, int32_t size)>;
    using onRequestData = std::function<void(int32_t offset, int32_t available)>;

    typedef struct tagCodecInfo {
        int32_t duration;
        int32_t videoPixFmt;
        int32_t videoWidth;
        int32_t videoHeight;
        int32_t audioSampleFmt;
        int32_t audioChannels;
        int32_t audioSampleRate;
    } CodecInfo;

public:
    FFmpegWrapper() {
        // init ffmpeg library instance
        FFmpegLibrary::INSTANCE();
    }

    void initDecoder(int32_t fileSize, uint32_t waitHeaderLength = 512 * 1024) {
        LOG_INFO("Start to init decoder, filesize={}, waitHeaderLength={}", fileSize, waitHeaderLength);

        if (waitHeaderLength_ > 0) {
            waitHeaderLength_ = waitHeaderLength;
        }

        if (fileSize >= 0) {
            fileSize_ = fileSize;
            fileName_ = "tmp-" + std::to_string(getTickCount()) + ".mp4";
            LOG_INFO("create file {}", fileName_);
            fp_ = fopen(fileName_.c_str(), "wb+");
            if (fp_ == nullptr) {
                LOG_INFO("Open file {} failed, err: {}.", fileName_, errno);
                raiseException(kErrorCode_Open_File_Error, "Open file");
            }
        } else {
            isStream_ = true;
            fifoSize_ = kDefaultFifoSize;
            fifo_     = av_fifo_alloc(fifoSize_);
        }

        LOG_INFO("Decoder initialized");
    }

    void uninitDecoder() {
        if (fp_ != nullptr) {
            fclose(fp_);
            fp_ = nullptr;
            remove(fileName_.c_str());
        }

        if (fifo_ != nullptr) {
            av_fifo_freep(&fifo_);
        }

        LOG_INFO("Decoder uninitialized.");
    }

    void openDecoder(bool hasVideo, bool hasAudio, onVideo videoCallback, onAudio audioCallback, onRequestData requestDataback, CodecInfo &codec) {
        LOG_INFO("Start open decoder, hasVideo({}), hasAudio({}).", hasVideo, hasAudio);

        avformatContext_ = avformat_alloc_context();
        customIoBuffer_  = (uint8_t *)av_mallocz(kCustomIoBufferSize);

        AVIOContext *ioContext = avio_alloc_context(customIoBuffer_, kCustomIoBufferSize, 0, (void *)this, FFmpegWrapper::ffReadCallback, nullptr,
                                                    FFmpegWrapper::ffSeekCallback);
        if (ioContext == nullptr) {
            LOG_INFO("avio_alloc_context failed.");
            raiseException(kErrorCode_FFmpeg_Error, "avio_alloc_context failed");
        }

        avformatContext_->pb    = ioContext;
        avformatContext_->flags = AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NONBLOCK;

        int32_t r = avformat_open_input(&avformatContext_, nullptr, nullptr, nullptr);
        if (r != 0) {
            raiseException(kErrorCode_FFmpeg_Error, "avformat_open_input failed: " + ffmpegError(r));
        }

        r = avformat_find_stream_info(avformatContext_, nullptr);
        if (r != 0) {
            raiseException(kErrorCode_FFmpeg_Error, "av_find_stream_info failed: " + ffmpegError(r));
        }

        for (uint32_t i = 0; i < avformatContext_->nb_streams; i++) {
            avformatContext_->streams[i]->discard      = AVDISCARD_DEFAULT;
            avformatContext_->streams[i]->need_parsing = AVSTREAM_PARSE_FULL;
        }

        codec.duration = 1000 * (avformatContext_->duration + 5000) / AV_TIME_BASE;

        if (hasVideo) {
            openCodecContext(avformatContext_, AVMEDIA_TYPE_VIDEO, &videoStreamIdx_, &videoCodecContext_);

            codec.videoPixFmt = videoCodecContext_->pix_fmt;
            codec.videoWidth  = videoCodecContext_->width;
            codec.videoHeight = videoCodecContext_->height;

            LOG_INFO("Open video codec context success, video stream index {} {}.", videoStreamIdx_, (void *)videoCodecContext_);
            LOG_INFO("Video stream index:{} pix_fmt:{} resolution:{}*{}.", videoStreamIdx_, codec.videoPixFmt, codec.videoWidth, codec.videoHeight);
        }

        if (hasAudio) {
            openCodecContext(avformatContext_, AVMEDIA_TYPE_AUDIO, &audioStreamIdx_, &audioCodecContext_);

            enum AVSampleFormat sampleFmt = audioCodecContext_->sample_fmt;
            if (av_sample_fmt_is_planar(sampleFmt)) {
                codec.audioSampleFmt = av_get_packed_sample_fmt(sampleFmt);
            } else {
                codec.audioSampleFmt = audioCodecContext_->sample_fmt;
            }
            codec.audioChannels   = audioCodecContext_->channels;
            codec.audioSampleRate = audioCodecContext_->sample_rate;

            LOG_INFO("Open audio codec context success, audio stream index {} {}.", audioStreamIdx_, (void *)audioCodecContext_);
            LOG_INFO("Audio stream index:{} sample_fmt:{} channel:{}, sample rate:{}.", audioStreamIdx_, codec.audioSampleFmt, codec.audioChannels,
                     codec.audioSampleRate);
        }

        av_seek_frame(avformatContext_, -1, 0, AVSEEK_FLAG_BACKWARD);

        videoSize_       = av_image_get_buffer_size(videoCodecContext_->pix_fmt, videoCodecContext_->width, videoCodecContext_->height, 1);
        videoBufferSize_ = videoSize_;
        yuvBuffer_       = (uint8_t *)av_mallocz(KDecodedDataTypeLength + KTimeStampStrLength + videoBufferSize_);
        pcmBufferSize_   = kInitialPcmBufferSize;
        pcmBuffer_       = (uint8_t *)av_mallocz(KDecodedDataTypeLength + KTimeStampStrLength + kInitialPcmBufferSize);
        avFrame_         = av_frame_alloc();

        // install callback function
        videoCallback_       = videoCallback;
        audioCallback_       = audioCallback;
        requestDataCallback_ = requestDataback;

        // start decode timer
        decodeTimer_.StartTimer(KDecodeTimerInterval, [=]() {
            // double check
            if (decoding_) {
                if (decoding_) {
                    try {
                        decodeOnePacket();
                    } catch (BizException &e) {
                        LOG_ERROR("Decode frame error, code={}, reason={}", e.code, e.msg);
                    } catch (std::exception &e) {
                        LOG_ERROR("Decode frame error, reason={}", e.what());
                    }
                }
            }
        });

        LOG_INFO("Decoder opened, duration {}s, picture size {}.", codec.duration, videoSize_);
    }

    void closeDecoder() {
        decodeTimer_.Expire();

        if (videoCodecContext_ != nullptr) {
            closeCodecContext(avformatContext_, videoCodecContext_, videoStreamIdx_);
            videoCodecContext_ = nullptr;
            LOG_INFO("Video codec context closed.");
        }

        if (audioCodecContext_ != nullptr) {
            closeCodecContext(avformatContext_, audioCodecContext_, audioStreamIdx_);
            audioCodecContext_ = nullptr;
            LOG_INFO("Audio codec context closed.");
        }

        if (avformatContext_ != nullptr) {
            AVIOContext *pb = avformatContext_->pb;
            if (pb != nullptr) {
                if (pb->buffer != nullptr) {
                    av_freep(&pb->buffer);
                    customIoBuffer_ = nullptr;
                }
                av_freep(&avformatContext_->pb);
                LOG_INFO("IO context released.");
            }

            avformat_close_input(&avformatContext_);
            avformatContext_ = nullptr;
            LOG_INFO("Input closed.");
        }

        if (yuvBuffer_ != nullptr) {
            av_freep(&yuvBuffer_);
        }

        if (pcmBuffer_ != nullptr) {
            av_freep(&pcmBuffer_);
        }

        if (avFrame_ != nullptr) {
            av_freep(&avFrame_);
        }

        LOG_INFO("All buffer released.");
    }

    void startDecode(bool start) { decoding_ = start; }

    int32_t sendData(uint8_t *buff, int32_t size) {
        if (buff == nullptr || size == 0) {
            raiseException(kErrorCode_Invalid_Param, "Invalid param");
        }
        std::unique_lock<std::mutex> lock(mutex_);
        return (isStream_) ? writeToFifo(buff, size) : writeToFile(buff, size);
    }

    void seekTo(int32_t ms, int32_t accurateSeek) {
        int64_t pts   = (int64_t)ms * 1000;
        accurateSeek_ = accurateSeek;

        int32_t ret = avformat_seek_file(avformatContext_, -1, INT64_MIN, pts, pts, AVSEEK_FLAG_BACKWARD);
        if (ret == -1) {
            raiseException(kErrorCode_FFmpeg_Error, ffmpegError(ret));
        }

        avcodec_flush_buffers(videoCodecContext_);
        avcodec_flush_buffers(audioCodecContext_);

        // Trigger seek callback
        AVPacket packet;
        av_init_packet(&packet);
        av_read_frame(avformatContext_, &packet);

        beginTimeOffset_ = (double)ms / 1000;
    }

    void decodeOnePacket() {
        if (avformatContext_ == nullptr) {
            return;
        }

        if (getAailableDataSize() <= 0) {
            return;
        }

        AVPacket packet;

        av_init_packet(&packet);
        common::RAII unref([&]() { av_packet_unref(&packet); });

        packet.data = nullptr;
        packet.size = 0;

        int r = av_read_frame(avformatContext_, &packet);
        if (r == AVERROR_EOF) {
            raiseException(kErrorCode_Eof, "Read frame EOF");
        }

        while (r == 0 && packet.size > 0) {
            int32_t decodedLen = 0;
            decodePacket(&packet, &decodedLen);
            if (decodedLen <= 0) {
                break;
            }
            packet.data += decodedLen;
            packet.size -= decodedLen;
        }
    }

private:
    static int32_t ffReadCallback(void *opaque, uint8_t *buf, int32_t buf_size) { return ((FFmpegWrapper *)opaque)->readCallback(buf, buf_size); }

    static int64_t ffSeekCallback(void *opaque, int64_t offset, int32_t whence) { return ((FFmpegWrapper *)opaque)->seekCallback(offset, whence); }

    void openCodecContext(AVFormatContext *fmtCtx, enum AVMediaType type, int32_t *streamIdx, AVCodecContext **decCtx) {
        int32_t ret         = 0;
        int32_t streamIndex = -1;
        AVStream *st        = nullptr;
        AVCodec *dec        = nullptr;
        AVDictionary *opts  = nullptr;

        LOG_INFO("Open codec context, type={}", av_get_media_type_string(type));

        ret = av_find_best_stream(fmtCtx, type, -1, -1, nullptr, 0);
        if (ret < 0) {
            raiseException(kErrorCode_FFmpeg_Error, "av_find_best_stream error, " + ffmpegError(ret));
        }

        streamIndex = ret;
        st          = fmtCtx->streams[streamIndex];

        dec = avcodec_find_decoder(st->codecpar->codec_id);
        // dec = avcodec_find_decoder_by_name("hevc");
        if (!dec) {
            ret = AVERROR(EINVAL);
            raiseException(kErrorCode_FFmpeg_Error, "avcodec_find_decoder_by_name error, " + ffmpegError(ret));
        }

        *decCtx = avcodec_alloc_context3(dec);
        if (!*decCtx) {
            ret = AVERROR(ENOMEM);
            raiseException(kErrorCode_FFmpeg_Error, "avcodec_alloc_context3 error, " + ffmpegError(ret));
        }

        if ((ret = avcodec_parameters_to_context(*decCtx, st->codecpar)) != 0) {
            raiseException(kErrorCode_FFmpeg_Error, "avcodec_parameters_to_context error, " + ffmpegError(ret));
        }

        av_dict_set(&opts, "refcounted_frames", "0", 0);
        av_dict_set(&opts, "threads", "4", 0);
        // av_dict_set(&opts, "probesize", std::to_string(waitHeaderLength_).c_str(), 0);

        if ((ret = avcodec_open2(*decCtx, dec, nullptr)) != 0) {
            raiseException(kErrorCode_FFmpeg_Error, "avcodec_open2 error, " + ffmpegError(ret));
        }

        *streamIdx = streamIndex;
        avcodec_flush_buffers(*decCtx);

        LOG_INFO("openCodecContext succeed");
    }

    void closeCodecContext(AVFormatContext *fmtCtx, AVCodecContext *decCtx, uint32_t streamIdx) {
        if (fmtCtx == nullptr || decCtx == nullptr) {
            return;
        }

        if (streamIdx < 0 || streamIdx >= fmtCtx->nb_streams) {
            return;
        }

        fmtCtx->streams[streamIdx]->discard = AVDISCARD_ALL;
        avcodec_close(decCtx);

        LOG_INFO("Close codec context");
    }

    void copyYuvData(AVFrame *frame, uint8_t *buffer, int32_t width, int32_t height) {
        uint8_t *src = nullptr;
        uint8_t *dst = buffer;
        int32_t i    = 0;

        if (frame == nullptr || buffer == nullptr) {
            raiseException(kErrorCode_Invalid_Param, "Invalid param");
        }

        if (!frame->data[0] || !frame->data[1] || !frame->data[2]) {
            raiseException(kErrorCode_Invalid_Param, "Invalid param");
        }

        for (i = 0; i < height; i++) {
            src = frame->data[0] + i * frame->linesize[0];
            memcpy(dst, src, width);
            dst += width;
        }

        for (i = 0; i < height / 2; i++) {
            src = frame->data[1] + i * frame->linesize[1];
            memcpy(dst, src, width / 2);
            dst += width / 2;
        }

        for (i = 0; i < height / 2; i++) {
            src = frame->data[2] + i * frame->linesize[2];
            memcpy(dst, src, width / 2);
            dst += width / 2;
        }
    }

    void copyPcmData(AVFrame *frame, uint8_t *buffer, uint32_t sampleSize) {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < frame->nb_samples; i++) {
            for (uint32_t ch = 0; ch < audioCodecContext_->channels; ch++) {
                memcpy(pcmBuffer_ + offset, frame->data[ch] + sampleSize * i, sampleSize);
                offset += sampleSize;
            }
        }
    }

    int32_t readCallback(uint8_t *data, int32_t len) {
        if (data == nullptr || len <= 0) {
            return -1;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        return (isStream_) ? readFromFifo(data, len) : readFromFile(data, len);
    }

    int64_t seekCallback(int64_t offset, int32_t whence) {
        std::unique_lock<std::mutex> lock(mutex_);
        return (isStream_) ? seekFifo(offset, whence) : seekFifo(offset, whence);
    }

    int64_t seekFifo(int64_t offset, int32_t whence) {
        if (!isStream_) {
            LOG_ERROR("Not stream");
            return -1;
        }

        LOG_INFO("Seek fifo, whence {}, offset {}, fifoSize {}", whence, offset, fifoSize_);

        /*
         * fifo原本不支持seek操作，但实况时，如果不支持seek操作会导致解析首帧视频时很慢，这里针对fifo做的seek做简单的操作
         * 当为SEEK_SET时，把read指针指向开始，fifo的空间最好设置大一些，避免覆盖
         * 暂时没想到其他办法，实属无奈^^
         */
        if (whence == SEEK_SET) {
            // 因为fifo是循环写的，但seek的时候传入的offset不是循环的，所以，这里转成循环后的offset
            offset = offset % fifoSize_;
            avFifoReadReset(fifo_);
        } else if (whence == SEEK_END) {
            // seek保护，避免内存越界，从末尾seek,最多可以seek到开始
            if ((fifo_->rptr + offset) < fifo_->buffer) {
                LOG_ERROR("Invalid fifo seek, offset={}, whence={}", offset, whence);
                return -1;
            }
        } else {
            LOG_ERROR("Unsupported whence {}, offset is {}", whence, offset);
            return -1;
        }

        av_fifo_drain(fifo_, offset);
        return offset;
    }

    int64_t seekFile(int64_t offset, int32_t whence) {
        int64_t ret     = -1;
        int64_t pos     = -1;
        int64_t req_pos = -1;

        if (whence != SEEK_END && whence != SEEK_SET && whence != SEEK_CUR) {
            LOG_ERROR("Unsupported whence {}, offset is {}", whence, offset);
            return -1;
        }

        if (isStream_ || fp_ == nullptr) {
            LOG_ERROR("Not file or file is null");
            return -1;
        }

        if (whence == AVSEEK_SIZE) {
            return fileSize_;
        }

        ret = fseek(fp_, (long)offset, whence);
        if (ret == -1) {
            return ret;
        }

        pos = (int64_t)ftell(fp_);
        if (pos < lastRequestOffset_ || pos > fileWritePos_) {
            lastRequestOffset_ = pos;
            fileReadPos_       = pos;
            fileWritePos_      = pos;
            req_pos            = pos;
            ret                = -1; // Forcing not to call read at once.
            requestDataCallback_(pos, getAailableDataSize());
            LOG_INFO("Will request {} and return {}.", pos, ret);
            return ret;
        }

        fileReadPos_ = pos;
        ret          = pos;

        if (requestDataCallback_ != nullptr) {
            requestDataCallback_(req_pos, getAailableDataSize());
        }

        return ret;
    }

    void avFifoReadReset(AVFifoBuffer *f) {
        // 重置fifo的读指针到开始
        f->rptr = f->buffer;
        f->rndx = 0;
    }

    int32_t roundUp(int32_t numToRound, int32_t multiple) { return (numToRound + multiple - 1) & -multiple; }

    void processDecodedVideoFrame(AVFrame *frame) {
        double timestamp = 0.0f;
        int32_t offset   = 0;

        if (frame == nullptr || videoCallback_ == nullptr || videoBufferSize_ <= 0) {
            raiseException(kErrorCode_Invalid_Param, "Invalid param");
        }

        if (videoCodecContext_->pix_fmt != AV_PIX_FMT_YUV420P && videoCodecContext_->pix_fmt != AV_PIX_FMT_YUVJ420P) {
            raiseException(kErrorCode_Invalid_Format, std::string("Unknown pixel format ") + std::to_string(videoCodecContext_->pix_fmt));
        }

        timestamp = (double)frame->pts * av_q2d(avformatContext_->streams[videoStreamIdx_]->time_base);

        if (accurateSeek_ && timestamp < beginTimeOffset_) {
            raiseException(kErrorCode_Old_Frame, "video timestamp " + std::to_string(timestamp) + "< " + std::to_string(beginTimeOffset_));
        }

        // set data type
        yuvBuffer_[0] = KVideoFrameFlag;
        offset += KDecodedDataTypeLength;
        // set timestamp
        memcpy(yuvBuffer_ + offset, timestamp2str(timestamp).c_str(), KTimeStampStrLength);
        offset += KTimeStampStrLength;
        // set data
        copyYuvData(frame, yuvBuffer_ + offset, videoCodecContext_->width, videoCodecContext_->height);
        // callback
        videoCallback_(yuvBuffer_, KDecodedDataTypeLength + KTimeStampStrLength + videoSize_);
    }

    void processDecodedAudioFrame(AVFrame *frame) {
        int32_t sampleSize    = 0;
        int32_t audioDataSize = 0;
        int32_t targetSize    = 0;
        int32_t offset        = 0;
        int32_t i             = 0;
        int32_t ch            = 0;
        double timestamp      = 0.0f;

        if (frame == nullptr || audioCallback_ == nullptr) {
            raiseException(kErrorCode_Invalid_Param, "Invalid param");
        }

        sampleSize = av_get_bytes_per_sample(audioCodecContext_->sample_fmt);
        if (sampleSize < 0) {
            raiseException(kErrorCode_Invalid_Data, "Invalid data");
        }

        audioDataSize = frame->nb_samples * audioCodecContext_->channels * sampleSize;
        if (pcmBufferSize_ < audioDataSize) {
            targetSize = roundUp(audioDataSize, 4);
            LOG_INFO("PCM buffer size {} not sufficient for data size {}, round up to target {}.", pcmBufferSize_, audioDataSize, targetSize);
            pcmBufferSize_ = targetSize;
            av_free(pcmBuffer_);
            pcmBuffer_ = (uint8_t *)av_mallocz(pcmBufferSize_ + KTimeStampStrLength + KDecodedDataTypeLength);
        }

        timestamp = (double)frame->pts * av_q2d(avformatContext_->streams[audioStreamIdx_]->time_base);

        if (accurateSeek_ && timestamp < beginTimeOffset_) {
            LOG_INFO("audio timestamp {} < {}", timestamp, beginTimeOffset_);
            raiseException(kErrorCode_Old_Frame, "Old frame");
        }

        // set data type
        pcmBuffer_[0] = KAudioFrameFlag;
        offset += KDecodedDataTypeLength;
        // set timestamp
        memcpy(pcmBuffer_ + offset, timestamp2str(timestamp).c_str(), KTimeStampStrLength);
        offset += KTimeStampStrLength;
        // set data
        copyPcmData(frame, pcmBuffer_ + offset, sampleSize);
        // callback
        audioCallback_(pcmBuffer_, KDecodedDataTypeLength + KTimeStampStrLength + audioDataSize);
    }

    void decodePacket(AVPacket *pkt, int32_t *decodedLen) {
        int32_t ret                  = 0;
        int32_t isVideo              = 0;
        AVCodecContext *codecContext = nullptr;

        if (pkt == nullptr || decodedLen == nullptr) {
            raiseException(kErrorCode_Invalid_Param, "Invalid param");
        }

        *decodedLen = 0;

        if (pkt->stream_index == videoStreamIdx_) {
            codecContext = videoCodecContext_;
            isVideo      = 1;
        } else if (pkt->stream_index == audioStreamIdx_) {
            codecContext = audioCodecContext_;
            isVideo      = 0;
        } else {
            raiseException(kErrorCode_Invalid_Data, "Invalid data");
        }

        ret = avcodec_send_packet(codecContext, pkt);
        if (ret < 0) {
            raiseException(kErrorCode_FFmpeg_Error, "avcodec_send_packet " + ffmpegError(ret));
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codecContext, avFrame_);
            if (ret == AVERROR(EAGAIN)) {
                return;
            } else if (ret == AVERROR_EOF) {
                raiseException(kErrorCode_Eof, "avcodec_receive_frame");
            } else if (ret < 0) {
                raiseException(kErrorCode_FFmpeg_Error, "avcodec_receive_frame");
            } else {
                if (isVideo) {
                    processDecodedVideoFrame(avFrame_);
                } else {
                    processDecodedAudioFrame(avFrame_);
                }
            }
        }

        *decodedLen = pkt->size;
    }

    int32_t readFromFile(uint8_t *data, int32_t len) {
        int32_t ret            = -1;
        int32_t availableBytes = 0;
        int32_t canReadLen     = 0;

        if (fp_ == nullptr) {
            return ret;
        }

        availableBytes = fileWritePos_ - fileReadPos_;
        if (availableBytes <= 0) {
            return ret;
        }

        fseek(fp_, fileReadPos_, SEEK_SET);
        canReadLen = MIN(availableBytes, len);
        int n      = fread(data, canReadLen, 1, fp_);
        fileReadPos_ += n;
        ret = canReadLen;

        return ret;
    }

    int32_t readFromFifo(uint8_t *data, int32_t len) {
        int32_t ret            = -1;
        int32_t availableBytes = 0;
        int32_t canReadLen     = 0;
        if (fifo_ == nullptr) {
            return ret;
        }

        int count = 50;
        while (count-- > 0 && av_fifo_size(fifo_) < len) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        availableBytes = av_fifo_size(fifo_);
        if (availableBytes <= 0) {
            LOG_WARN("No more data to read, available bytes is {}", availableBytes);
            return AVERROR(EAGAIN);
        }

        canReadLen = MIN(availableBytes, len);
        av_fifo_generic_read(fifo_, data, canReadLen, nullptr);
        ret = canReadLen;

        // LOG_INFO("Read from fifo, size {}, left {}", ret, av_fifo_size(fifo_));

        return ret;
    }

    int32_t writeToFile(uint8_t *buff, int32_t size) {
        int32_t ret           = 0;
        int64_t leftBytes     = 0;
        int32_t canWriteBytes = 0;
        if (fp_ == nullptr) {
            ret = -1;
            return ret;
        }

        leftBytes = fileSize_ - fileWritePos_;
        if (leftBytes <= 0) {
            return ret;
        }

        canWriteBytes = MIN(leftBytes, size);
        fseek(fp_, fileWritePos_, SEEK_SET);
        fwrite(buff, canWriteBytes, 1, fp_);
        fileWritePos_ += canWriteBytes;
        ret = canWriteBytes;

        return ret;
    }

    int32_t writeToFifo(uint8_t *buff, int32_t size) {
        int32_t ret = 0;
        if (fifo_ == nullptr) {
            return -1;
        }

        int64_t leftSpace = av_fifo_space(fifo_);
        if (leftSpace < size) {
            int32_t growSize = 0;
            do {
                leftSpace += fifoSize_;
                growSize += fifoSize_;
                fifoSize_ += fifoSize_;
            } while (leftSpace < size);

            av_fifo_grow(fifo_, growSize);

            LOG_INFO("Fifo size growed to {}.", fifoSize_);
            if (fifoSize_ >= kMaxFifoSize) {
                LOG_INFO("[Warn] Fifo size larger than {}.", kMaxFifoSize);
            }
        }

        ret = av_fifo_generic_write(fifo_, buff, size, nullptr);
        // LOG_INFO("Write fifo_ size {}, left {}", ret, av_fifo_size(fifo_));
        return ret;
    }

    int32_t getAailableDataSize() {
        if (isStream_) {
            return fifo_ == nullptr ? 0 : av_fifo_size(fifo_);
        } else {
            return fileWritePos_ - fileReadPos_;
        }
    }

    std::string ffmpegError(int32_t r) {
        char err_info[64] = {0};
        av_strerror(r, err_info, 32);
        return "<" + std::to_string(r) + "> " + std::string(err_info);
    }

    uint64_t getTickCount() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * (unsigned long)1000 + ts.tv_nsec / 1000000;
    }

    std::string timestamp2str(double timestamp) {
        char ss[KTimeStampStrLength] = {0};
        sprintf(ss, "%.6lf", timestamp);
        return std::string(ss, ss + sizeof(ss));
    }

    void raiseException(ErrorCode code, const std::string &msg) { throw BizException((int32_t)code, msg); }

private:
    // ffmpeg
    AVFormatContext *avformatContext_  = nullptr;
    AVCodecContext *videoCodecContext_ = nullptr;
    AVCodecContext *audioCodecContext_ = nullptr;
    uint8_t *customIoBuffer_           = nullptr;
    AVFrame *avFrame_                  = nullptr;
    int32_t videoStreamIdx_            = -1;
    int32_t audioStreamIdx_            = -1;
    uint8_t *yuvBuffer_                = nullptr;
    int32_t videoBufferSize_           = 0;
    int32_t videoSize_                 = 0;
    uint8_t *pcmBuffer_                = nullptr;
    int32_t pcmBufferSize_             = 0;

    // callback
    onVideo videoCallback_             = nullptr;
    onAudio audioCallback_             = nullptr;
    onRequestData requestDataCallback_ = nullptr;

    // common
    int32_t waitHeaderLength_ = 512 * 1024;
    bool isStream_            = false;
    bool decoding_            = false;
    common::Timer decodeTimer_;
    std::mutex mutex_;

    // file
    std::string fileName_;
    FILE *fp_                  = nullptr;
    int64_t fileSize_          = 0;
    int64_t fileReadPos_       = 0;
    int64_t fileWritePos_      = 0;
    int64_t lastRequestOffset_ = 0;
    double beginTimeOffset_    = 0;
    int32_t accurateSeek_      = 0;

    // stream
    AVFifoBuffer *fifo_ = nullptr;
    int32_t fifoSize_   = 0;
};

} // namespace decoder