#pragma once

#include <string>
#include "json/json.hpp"

namespace decoder {
using json = nlohmann::json;

// business exception
class BizException : public std::exception {
public:
    int code;
    std::string msg;

public:
    BizException(int code, const std::string &msg) {
        this->code = code;
        this->msg  = msg;
    }
};

//---------------------------------------------------------------------------
typedef struct tagBaseRequest {
    std::string cmd;
} BaseRequest;

typedef struct tagBaseResponse {
    int code;
    std::string msg;
    std::string cmd;

    tagBaseResponse() { code = 0; }

    tagBaseResponse(const std::string &cmd, int code, const std::string &msg) {
        this->cmd  = cmd;
        this->code = code;
        this->msg  = msg;
    }
} BaseResponse;

void from_json(const json &j, BaseRequest &p) {
    try {
        p.cmd = j.at("cmd").get<std::string>();
    } catch (std::exception &e) {
    }
}

void to_json(json &j, const BaseRequest &p) {
    j["cmd"] = p.cmd;
}

void to_json(json &j, const BaseResponse &p) {
    j["cmd"]  = p.cmd;
    j["code"] = p.code;
    j["msg"]  = p.msg;
}

void from_json_base(const json &j, BaseRequest &p) {
    from_json(j, p);
}

void to_json_base(json &j, const BaseResponse &p) {
    to_json(j, p);
}

void to_json_base(json &j, const BaseRequest &p) {
    to_json(j, p);
}

//---------------------------------------------------------------------------
typedef struct tagInitDecoderRequest : public BaseRequest {
    int fileSize;
    int waitHeaderLength;
} InitDecoderRequest;

void from_json(const json &j, InitDecoderRequest &p) {
    from_json_base(j, p);

    try {
        p.fileSize         = j.at("fileSize").get<int>();
        p.waitHeaderLength = j.at("waitHeaderLength").get<int>();
    } catch (std::exception &e) {
    }
}

//---------------------------------------------------------------------------
typedef struct tagOpenDecoderRequest : public BaseRequest {
    bool hasVideo;
    bool hasAudio;
} OpenDecoderRequest;

void from_json(const json &j, OpenDecoderRequest &p) {
    from_json_base(j, p);
    try {
        p.hasVideo = j.at("hasVideo").get<bool>();
        p.hasAudio = j.at("hasAudio").get<bool>();
    } catch (std::exception &e) {
    }
}

//---------------------------------------------------------------------------
typedef struct tagOpenDecoderResponse : public BaseResponse {
    int duration;
    int videoPixFmt;
    int videoWidth;
    int videoHeight;
    int audioSampleFmt;
    int audioChannels;
    int audioSampleRate;

    tagOpenDecoderResponse() { cmd = "openDecoder"; }

    tagOpenDecoderResponse(int duration, int videoPixFmt, int videoWidth, int videoHeight, int audioSampleFmt, int audioChannels, int audioSampleRate) {
        cmd                   = "openDecoder";
        this->duration        = duration;
        this->videoPixFmt     = videoPixFmt;
        this->videoWidth      = videoWidth;
        this->videoHeight     = videoHeight;
        this->audioSampleFmt  = audioSampleFmt;
        this->audioChannels   = audioChannels;
        this->audioSampleRate = audioSampleRate;
    }
} OpenDecoderReponse;

void to_json(json &j, const OpenDecoderReponse &p) {
    to_json_base(j, p);

    j["duration"]        = p.duration;
    j["videoPixFmt"]     = p.videoPixFmt;
    j["videoWidth"]      = p.videoWidth;
    j["videoHeight"]     = p.videoHeight;
    j["audioSampleFmt"]  = p.audioSampleFmt;
    j["audioChannels"]   = p.audioChannels;
    j["audioSampleRate"] = p.audioSampleRate;
}

//---------------------------------------------------------------------------
typedef struct tagRequestDataRequest : public BaseRequest {
    int32_t offset;
    int32_t available;

    tagRequestDataRequest() { cmd = "requestData"; }

    tagRequestDataRequest(int32_t offset, int32_t available) {
        cmd             = "requestData";
        this->offset    = offset;
        this->available = available;
    }
} RequestDataRequest;

void to_json(json &j, const RequestDataRequest &p) {
    to_json_base(j, p);

    j["offset"]    = p.offset;
    j["available"] = p.available;
}

} // namespace decoder