#pragma once

#include <mutex>
#include <thread>
#include <exception>
#include <ctime>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <functional>

#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

namespace common {

class WatchdogExp : public std::exception {};

class Watchdog {

public:
    Watchdog(unsigned int timeoutPeriod = 10) {
        int ret = -1;

        if (resetMembers(timeoutPeriod) < 0) {
            throw WatchdogExp();
        }
    }
    void start() {
        while (true) {
            // if it is child, startOnce will return non-zero
            // else, return zero to indicate wdt want to restart worker
            if (startOnce()) {
                break;
            } else {
                std::cout << "Restart..." << std::endl;
                // reset all members to restart again
                resetMembers(timeoutPeriod_);
            }
        }
    }
    int kick() {
        std::unique_lock<std::mutex> lock(mutex_);
        int ret         = -1;
        const char data = 'Y';

        ret = ::write(writePipe_, &data, 1);
        if (ret != 1) {
            return -1;
        }
        return 1;
    }

protected:
    int resetMembers(unsigned int timeoutPeriod) {
        timeoutPeriod_  = timeoutPeriod;
        lastKickedTime_ = std::time(NULL);
        workerPid_      = -1;
        isStop_         = false;

        int pfd[2];
        if (::pipe(pfd) == -1) {
            return -1;
        }

        readPipe_  = pfd[0];
        writePipe_ = pfd[1];

        return 0;
    }

    int setNonblocking(int fd) {
        int flags;
        if (-1 == (flags = ::fcntl(fd, F_GETFL, 0))) {
            flags = 0;
        }
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    int startOnce() {
        workerPid_ = ::fork();

        if (workerPid_ < 0) {
            throw WatchdogExp();
        } else if (workerPid_ == 0) {
            // child, go back to do its work
            ::close(readPipe_);
            return 1;
        }

        ::close(writePipe_);

        if (setNonblocking(readPipe_) != 0) {
            throw WatchdogExp();
        }

        kickedCheckThread_ = std::thread(std::bind(&Watchdog::kickedChecker, this));

        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);

                time_t curTime = time(NULL);
                if ((curTime - lastKickedTime_) > timeoutPeriod_) {
                    std::cout << "Timeout!" << std::endl;
                    isStop_ = true;
                    ::kill(workerPid_, SIGKILL);
                    ::waitpid(workerPid_, NULL, 0);
                    ::close(readPipe_);
                    break;
                }
            }

            ::sleep(1);
        }

        kickedCheckThread_.join();

        return 0;
    }

    void kickedChecker() {
        fd_set readfds;
        char err_str[64];

        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (isStop_) {
                    break;
                }
            }

            FD_ZERO(&readfds);
            FD_SET(readPipe_, &readfds);

            struct timeval tv;
            tv.tv_sec  = 1;
            tv.tv_usec = 0;

            int ret = select(readPipe_ + 1, &readfds, NULL, NULL, &tv);
            if (ret == -1) {
                break;
            } else if (ret) {
                char data  = '\0';
                int n_read = 0;

                if (FD_ISSET(readPipe_, &readfds)) {
                    n_read = ::read(readPipe_, &data, 1);
                    if (data == 'Y') {
                        // std::cout << "sense kicked" << std::endl;
                        std::unique_lock<std::mutex> lock(mutex_);
                        lastKickedTime_ = std::time(NULL);
                    }
                }
            }
        }
    }

private:
    unsigned int timeoutPeriod_;
    time_t lastKickedTime_;
    int readPipe_;
    int writePipe_;
    std::thread kickedCheckThread_;
    pid_t workerPid_;
    std::mutex mutex_;
    bool isStop_;
};

} // namespace common
