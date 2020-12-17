#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <cstdint>
#include <sstream>

namespace common {

class CountTimer {
public:
    CountTimer(const std::string &name, int min = 0) : name_(name), min_(min) { start_ = std::chrono::steady_clock::now(); }

    ~CountTimer() {
        end_      = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(end_ - start_).count();
        if (diff > min_) {
            std::cout << "[" << name_ << "](" << tid() << ") -> " << readableTime(diff) << std::endl;
        }
    }

private:
    std::string readableTime(long microseconds) {
        float s;
        std::string unit;
        if (microseconds / (1000 * 1000) >= 1) {
            s    = (float)microseconds / (1000 * 1000);
            unit = "s";
        } else if (microseconds / 1000 >= 1) {
            s    = (float)microseconds / 1000;
            unit = "ms";
        } else {
            unit = "us";
        }
        return std::to_string(s) + unit;
    }

    uint64_t tid() {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        std::string stid = oss.str();
        return std::stoull(stid);
    }

    std::string name_;
    int min_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point end_;
};

} // namespace  common
