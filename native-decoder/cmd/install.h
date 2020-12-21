#pragma once

#include <unistd.h>
#include <string>

class InstallTool {
public:
    virtual void svcInstall(const std::string &program) = 0;
};

namespace detail {

#ifdef __linux__
class LinuxInstallTool : public InstallTool {
public:
    void svcInstall(const std::string &program) override {}
};

#endif

#ifdef WIN32

    #include <tlhelp32.h>

class WindowsInstallTool : public InstallTool {
public:
    void svcInstall(const std::string &program) override {
        auto srcFile = program;
        auto pos     = program.find_last_of("\\") == std::string::npos ? 0 : program.find_last_of("\\") + 1;
        auto exe     = program.substr(pos, program.length());
        auto dstFile = std::string(::getenv("APPDATA")) + "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\" + exe;
        if (srcFile == dstFile) {
            // hide myself
            ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
            // already installed
            return;
        }

        killOtherProcessByName(exe.c_str());

        // SetConsoleOutputCP(CP_UTF8);

        std::cout << "#------------------------------------------------#" << std::endl;
        std::cout << "# Begin to install service, please wait......    #" << std::endl;

        std::ifstream in(srcFile.c_str(), std::ios::binary);
        std::ofstream out(dstFile.c_str(), std::ios::binary);
        if (!in) {
            std::cerr << "open file " << srcFile << std::endl;
            return;
        }
        if (!out) {
            std::cerr << "open file " << dstFile << std::endl;
            return;
        }
        out << in.rdbuf();
        in.close();
        out.close();

        std::cout << "# Service installed successfully [^^]            #" << std::endl;
        std::cout << "#------------------------------------------------#" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(3));

        // hide myself
        ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
    }

private:
    bool killOtherProcessByName(const char *lpszProcessName) {
        unsigned int pid = -1;
        bool retval      = true;

        if (lpszProcessName == NULL) return false;

        DWORD dwRet      = 0;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 processInfo;
        processInfo.dwSize = sizeof(PROCESSENTRY32);
        int flag           = Process32First(hSnapshot, &processInfo);

        // Find the process with name as same as lpszProcessName
        while (flag != 0) {
            if (strcmp(processInfo.szExeFile, lpszProcessName) == 0) {
                // Terminate the process.
                pid = processInfo.th32ProcessID;
                if (getpid() != pid) {
                    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, true, pid);
                    if (TerminateProcess(hProcess, 0) != true) { // Failed to terminate it.
                        retval = false;
                        break;
                    }
                }
            }

            flag = Process32Next(hSnapshot, &processInfo);
        } // while (flag != 0)

        CloseHandle(hSnapshot);

        if (pid == -1) return false;

        return retval;
    }
};

#endif

} // namespace detail

class InstallToolFactory {
public:
    static std::shared_ptr<InstallTool> newInstallTool() {
#ifdef __linux__
        return std::make_shared<detail::LinuxInstallTool>();
#else
        return std::make_shared<detail::WindowsInstallTool>();
#endif
    }
};
