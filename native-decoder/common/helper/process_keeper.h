#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <functional>

namespace common {
#define CHILD_ARG_FLAG "-child"

class ProcessKeeper {
public:
    ProcessKeeper() : processId_(0), child_(false) {}

    void init(int argc, char **argv) {
        this->applicationName_ = std::string(argv[0]);

        // parse argument
        for (int i = 1; i < argc; i++) {
            if (std::string(argv[i]) == CHILD_ARG_FLAG) {
                child_ = true;
                continue;
            }
            arguments_.push_back(argv[i]);
        }
    }

    int run(std::function<int(int, char **)> childEntrypoint) {
        if (child_) {
            int newArgc = arguments_.size();
            std::unique_ptr<char *[]> newArgv(new char *[newArgc]);
            for (uint8_t i = 0; i < arguments_.size(); i++) {
                newArgv[i] = (char *)arguments_[i].c_str();
            }
            return childEntrypoint(newArgc, newArgv.get());
        }

        // set child flag
        arguments_.push_back(CHILD_ARG_FLAG);

        // check child progress
        keepAliveThread_.reset(new std::thread([=]() {
            while (true) {
                if (!this->isOk()) {
                    std::cout << "Restart program " << this->getApplicationName() << std::endl;
                    this->restart();
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }));
        keepAliveThread_->join();
        return 0;
    }

    std::string getApplicationName() const { return this->applicationName_; }

    bool isChild() const { return child_; }

protected:
    virtual bool isOk()    = 0;
    virtual void restart() = 0;

protected:
    bool child_;
    uintptr_t processId_;
    std::string applicationName_;
    std::vector<std::string> arguments_;
    std::shared_ptr<std::thread> keepAliveThread_;
};

namespace detail {

#ifdef __linux__

    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <sys/types.h>

class LinuxProcessKeeperImpl : public ProcessKeeper {
    bool isOk() override {
        if (processId_ != 0) {
            return access(getProcCmdlinePath(processId_).c_str(), 0) == 0;
        } else {
            // first time
            return false;
        }
    }

    void restart() override {
        int pid = fork();
        if (pid < 0) {
            throw std::exception();
        } else if (pid == 0) {
            // child
            std::unique_ptr<char *[]> args(new char *[arguments_.size() + 2]);
            uint8_t i = 0;
            args[0]   = (char *)this->applicationName_.c_str();
            for (i = 0; i < arguments_.size(); i++) {
                args[i + 1] = (char *)arguments_[i].c_str();
            }
            args[i + 1] = nullptr;
            execvp(this->applicationName_.c_str(), args.get());
        } else {
            this->processId_ = pid;
            waitpid(this->processId_, nullptr, 0);
        }
    }

private:
    std::string getProcCmdlinePath(uintptr_t pid) {
        std::stringstream stm;
        stm << "/proc/" << pid << "/cmdline";
        return stm.str();
    }
};

#endif

#ifdef WIN32
    #include <windows.h>
    #include <psapi.h>

class WindowsProcessKeeperImpl : public ProcessKeeper {
    const uint32_t StillAlive = 259;

public:
    bool isOk() {
        if (processId_ != 0) {
            HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, this->processId_);
            if (processHandle != nullptr) {
                DWORD exitCode = StillAlive;
                if (GetExitCodeProcess(processHandle, &exitCode) && exitCode == StillAlive) {
                    return true;
                }
            }
        }

        return false;
    }

    void restart() {
        std::string commandLine = buildCommandLine();
        std::vector<char> buffer(std::begin(commandLine), std::end(commandLine));
        buffer.push_back(0);

        STARTUPINFO startupInfo                = {sizeof(STARTUPINFO)};
        PROCESS_INFORMATION processInformation = {0};
        bool creationResult = CreateProcessA(nullptr, &buffer[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInformation);
        if (creationResult) {
            CloseHandle(processInformation.hProcess);
            this->processId_ = processInformation.dwProcessId;
            updateProcessInfo();
        }
    }

private:
    void updateProcessInfo() {
        HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId_);
        if (processHandle != nullptr) {
            std::vector<char> buffer(1024);
            size_t size = GetModuleFileNameEx(processHandle, nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
            if (size > buffer.size()) {
                buffer.resize(size + 1);
                if (GetModuleFileNameEx(processHandle, nullptr, &buffer[0], static_cast<DWORD>(size)) == size) {
                    this->applicationName_ = std::string(&buffer[0], size);
                }
            } else {
                this->applicationName_ = std::string(&buffer[0], size);
            }

            CloseHandle(processHandle);
        }
    };

    std::string buildCommandLine() {
        std::ostringstream output;
        output << this->applicationName_ << " ";
        for (const auto &argument : this->arguments_) {
            output << argument << " ";
        }
        return output.str();
    }
};

#endif

} // namespace detail

class ProcessKeeperFactory {
public:
    static std::shared_ptr<ProcessKeeper> newProcessKeeper() {
#ifdef __linux__
        return std::make_shared<detail::LinuxProcessKeeperImpl>();
#else
        return std::make_shared<detail::WindowsProcessKeeperImpl>();
#endif
    }
};

} // namespace  common