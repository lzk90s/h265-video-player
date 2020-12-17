#pragma once

#include <string>
#include <memory>

#include "common/helper/singleton.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

namespace spd = spdlog;

namespace common {

class Logger {
public:
    Logger() : Logger("default") {}

    Logger(const std::string &moduleLibraryName) {
        // delegate = spd::rotating_logger_mt(moduleName, ".", 1048576 * 5, 3);
        delegate = spd::stdout_logger_mt(moduleLibraryName);

        delegate->flush_on(parseLevel());
        spd::set_level(parseLevel());
        spd::set_pattern("[%Y-%m-%d %H:%M:%S] [%n-%t] [%^%l%$] %v");
    }

    ~Logger() { spd::drop(moduleName); }

    std::shared_ptr<spdlog::logger> getLogger() { return delegate; }

private:
    spd::level::level_enum parseLevel() {
        spd::level::level_enum level = spdlog::level::info;
        const char *levelEnv         = getenv("SPD_LOG_LEVEL");
        if (nullptr != levelEnv && strlen(levelEnv) > 0) {
            level = spd::level::from_str(levelEnv);
        }
        return level;
    }

private:
    std::string moduleName;
    std::shared_ptr<spdlog::logger> delegate;
};

typedef Singleton<Logger> LoggerSingleton;

} // namespace common

#define LOG_TRACE(...)    common::LoggerSingleton::getInstance().getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    common::LoggerSingleton::getInstance().getLogger()->debug(__VA_ARGS__)
#define LOG_ERROR(...)    common::LoggerSingleton::getInstance().getLogger()->error(__VA_ARGS__)
#define LOG_WARN(...)     common::LoggerSingleton::getInstance().getLogger()->warn(__VA_ARGS__)
#define LOG_INFO(...)     common::LoggerSingleton::getInstance().getLogger()->info(__VA_ARGS__)
#define LOG_CRITICAL(...) common::LoggerSingleton::getInstance().getLogger()->critical(__VA_ARGS__)
