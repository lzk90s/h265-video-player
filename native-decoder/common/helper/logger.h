#pragma once

#include <string>
#include <memory>

#include "common/helper/singleton.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"

namespace common {

class Logger {
public:
    ~Logger() { reset(); }

    std::shared_ptr<spdlog::logger> getLogger() { return loggerImpl_; }

    void init(const std::string &name, const std::string &pattern = "[%Y-%m-%d %H:%M:%S] [%n-%P-%t] [%^%l%$] %v") {
        this->name_ = name;
        reset();

        auto consoleSink  = std::make_shared<spdlog::sinks::stdout_sink_mt>();
        auto rotatingSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(getDefaultLogFile(), 1048576 * 5, 3);
        consoleSink->set_pattern(pattern);
        rotatingSink->set_pattern(pattern);

        std::vector<spdlog::sink_ptr> sinks{consoleSink, rotatingSink};
        loggerImpl_ = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        loggerImpl_->flush_on(parseLevel());
        loggerImpl_->set_level(parseLevel());
    }

private:
    void reset() {
        if (nullptr != loggerImpl_) {
            loggerImpl_.reset();
            spdlog::drop(name_);
        }
    }

    spdlog::level::level_enum parseLevel() {
        spdlog::level::level_enum level = spdlog::level::info;
        const char *levelEnv            = getenv("SPD_LOG_LEVEL");
        if (nullptr != levelEnv && strlen(levelEnv) > 0) {
            level = spdlog::level::from_str(levelEnv);
        }
        return level;
    }

    std::string getDefaultLogFile() {
#ifdef WIN32
        return std::string(::getenv("HOMEDRIVE")) + std::string(::getenv("HOMEPATH")) + "\\" + name_ + ".log";
#else
        return std::string("/var/log/") + name_ + ".log";
#endif
    }

private:
    std::string name_;
    std::shared_ptr<spdlog::logger> loggerImpl_ = nullptr;
};

Logger &defaultLogger() {
    return common::Singleton<Logger>::getInstance();
}

} // namespace common

#define LOG_TRACE(...)    common::defaultLogger().getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    common::defaultLogger().getLogger()->debug(__VA_ARGS__)
#define LOG_ERROR(...)    common::defaultLogger().getLogger()->error(__VA_ARGS__)
#define LOG_WARN(...)     common::defaultLogger().getLogger()->warn(__VA_ARGS__)
#define LOG_INFO(...)     common::defaultLogger().getLogger()->info(__VA_ARGS__)
#define LOG_CRITICAL(...) common::defaultLogger().getLogger()->critical(__VA_ARGS__)
