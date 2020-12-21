
#include <map>
#include <memory>

#include <websocketpp/server.hpp>

#include "server/ffmpeg_wrapper.h"
#include "server/pojo.h"
#include "server/server_config.h"

#include "common/helper/logger.h"
#include "common/helper/threadpool.h"

namespace decoder {
namespace ws = websocketpp;

class DecodeServer {
public:
    using WsServer         = ws::server<ws::config::ServerConfig>;
    using WsConnection     = ws::connection_hdl;
    using WsOpcode         = ws::frame::opcode::value;
    using FFmpegWrapperPtr = std::shared_ptr<FFmpegWrapper>;
    using ConnMap          = std::map<WsConnection, FFmpegWrapperPtr, std::owner_less<WsConnection>>;
    using TextMsgProc      = std::function<void(FFmpegWrapperPtr ffmpeg, WsConnection hdl, WsServer::message_ptr msg)>;

    DecodeServer() {}

    ~DecodeServer() { endpoint_.stop_listening(); }

    void run(uint16_t port) {
        // Initialize the Asio transport policy
        endpoint_.init_asio();

        // set up access channels to only log interesting things
        endpoint_.clear_access_channels(ws::log::alevel::all);
        endpoint_.set_access_channels(ws::log::alevel::access_core);
        endpoint_.set_access_channels(ws::log::alevel::app);
        endpoint_.set_reuse_addr(true);

        using std::placeholders::_1;
        using std::placeholders::_2;
        using std::placeholders::_3;

        // Bind the handlers we are using
        endpoint_.set_open_handler(bind(&DecodeServer::onOpen, this, _1));
        endpoint_.set_close_handler(bind(&DecodeServer::onClose, this, _1));
        endpoint_.set_message_handler(bind(&DecodeServer::onMessage, this, _1, _2));

        // init textProcs
        textProcs_["initDecoder"]   = std::bind(&DecodeServer::initDecoder, this, _1, _2, _3);
        textProcs_["uninitDecoder"] = std::bind(&DecodeServer::uninitDecoder, this, _1, _2, _3);
        textProcs_["openDecoder"]   = std::bind(&DecodeServer::openDecoder, this, _1, _2, _3);
        textProcs_["closeDecoder"]  = std::bind(&DecodeServer::closeDecoder, this, _1, _2, _3);
        textProcs_["startDecode"]   = std::bind(&DecodeServer::startDecode, this, _1, _2, _3);
        textProcs_["stopDecode"]    = std::bind(&DecodeServer::stopDecode, this, _1, _2, _3);

        std::stringstream ss;
        ss << "Running server on port " << port;
        endpoint_.get_alog().write(ws::log::alevel::app, ss.str());

        // listen on specified port
        endpoint_.listen(port);

        // Start the server accept loop
        endpoint_.start_accept();

        // Start the ASIO io_service run loop
        ioThreads_.clear();
        for (std::size_t c = 1; c < ws::config::ServerConfig::iothrnum; c++) {
            ioThreads_.emplace_back([this]() {
                try {
                    endpoint_.run();
                } catch (ws::exception const &e) {
                    std::cout << e.what() << std::endl;
                }
            });
        }

        for (auto &t : ioThreads_) {
            t.join();
        }
    }

    void stop() { endpoint_.stop_listening(); }

    void onOpen(WsConnection hdl) {
        LOG_INFO("New connection {}", hdl.lock().get());
        connections_.emplace(hdl, std::make_shared<FFmpegWrapper>());
    }

    void onClose(WsConnection hdl) {
        LOG_INFO("Close connection {}", hdl.lock().get());
        connections_.erase(hdl);
    }

    void onMessage(WsConnection hdl, WsServer::message_ptr msg) {
        if (msg->get_opcode() == WsOpcode::text) {
            onTextMsg(connections_[hdl], hdl, msg);
        } else if (msg->get_opcode() == WsOpcode::binary) {
            onBinaryMsg(connections_[hdl], hdl, msg);
        }
    }

private:
    void onTextMsg(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) {
        std::string jsonStr = msg->get_payload();
        // LOG_INFO("Process request {}", jsonStr);

        auto req = json::parse(jsonStr).get<BaseRequest>();
        if (textProcs_.find(req.cmd) == textProcs_.end()) {
            LOG_WARN("Unknown command {}", req.cmd);
            return;
        }

        try {
            textProcs_[req.cmd](ffmpegWrapper, hdl, msg);
        } catch (BizException &e) {
            LOG_ERROR("BizException: code={}, msg={}", e.code, e.msg);
            sendMsg(hdl, ((json)BaseResponse(req.cmd, e.code, e.msg)).dump(), msg->get_opcode());
        } catch (std::exception &e) {
            LOG_ERROR("OtherException: msg={}", e.what());
            sendMsg(hdl, ((json)BaseResponse(req.cmd, -1, e.what())).dump(), msg->get_opcode());
        }
    }

    void onBinaryMsg(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) {
        auto data = msg->get_payload();
        ffmpegWrapper->sendData((uint8_t *)data.c_str(), data.length());
    }

    void initDecoder(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) {
        auto j = json::parse(msg->get_payload());
        auto o = j.get<InitDecoderRequest>();
        ffmpegWrapper->initDecoder(o.fileSize, o.waitHeaderLength);
    }

    void uninitDecoder(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) { ffmpegWrapper->uninitDecoder(); }

    void openDecoder(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) {
        auto j = json::parse(msg->get_payload());
        auto o = j.get<OpenDecoderRequest>();

        FFmpegWrapper::CodecInfo codecInfo;
        ffmpegWrapper->openDecoder(
            // has video/audio
            o.hasVideo, o.hasAudio,
            // video callback
            [=](uint8_t *buff, int32_t size) { sendMsg(hdl, (const uint8_t *)buff, size, WsOpcode::binary); },
            // audio callback
            [=](uint8_t *buff, int32_t size) { sendMsg(hdl, (const uint8_t *)buff, size, WsOpcode::binary); },
            // request data callback
            [=](int32_t offset, int32_t available) { sendMsg(hdl, ((json)RequestDataRequest(offset, available)).dump(), WsOpcode::text); },
            // codec output
            codecInfo);

        OpenDecoderReponse rspObj(/**video*/ codecInfo.duration, codecInfo.videoPixFmt, codecInfo.videoWidth, codecInfo.videoHeight,
                                  /**audio*/ codecInfo.audioSampleFmt, codecInfo.audioChannels, codecInfo.audioSampleRate);
        sendMsg(hdl, ((json)rspObj).dump(), msg->get_opcode());
    }

    void closeDecoder(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) { ffmpegWrapper->closeDecoder(); }

    void startDecode(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) { ffmpegWrapper->startDecode(true); }

    void stopDecode(FFmpegWrapperPtr ffmpegWrapper, WsConnection hdl, WsServer::message_ptr msg) { ffmpegWrapper->startDecode(false); }

    std::string timestamp2str(double timestamp) {
        char ss[16] = {0};
        sprintf(ss, "%.6lf", timestamp);
        return std::string(ss, ss + sizeof(ss));
    }

    int32_t sendMsg(WsConnection hdl, const uint8_t *buf, int32_t size, WsOpcode opcode) {
        endpoint_.send(hdl, buf, size, opcode);
        return size;
    }

    int32_t sendMsg(WsConnection hdl, const std::string &msg, WsOpcode opcode) {
        endpoint_.send(hdl, msg.c_str(), msg.size(), opcode);
        return msg.size();
    }

private:
    std::vector<std::thread> ioThreads_;
    WsServer endpoint_;
    ConnMap connections_;
    std::map<std::string, TextMsgProc> textProcs_;
};

} // namespace decoder
