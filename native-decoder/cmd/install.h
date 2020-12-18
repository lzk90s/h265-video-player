#pragma once

#include <string>

#ifdef __linux__
#else
    #include <tlhelp32.h>
#endif

class InstallTool {
public:
    void svcInstall(const std::string &program) {
#ifdef __linux__
        // use systemd
#else
        auto srcFile = program;
        auto pos     = program.find_last_of("\\") == std::string::npos ? 0 : program.find_last_of("\\") + 1;
        auto exe     = program.substr(pos, program.length());
        auto dstFile = std::string(::getenv("APPDATA")) + "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\" + exe;
        if (srcFile == dstFile) {
            // hide myself
            ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
            // 路径相同，说明是已经安装好的
            return;
        }

        killOtherProcessByName(exe.c_str());

        SetConsoleOutputCP(CP_UTF8);
        std::cout << "开始安装，请按任意键继续，安装完成后会自动关闭" << std::endl;
        (void)getchar();

        LOG_INFO("Install server to {}", dstFile);

        std::ifstream in(srcFile.c_str(), std::ios::binary);
        std::ofstream out(dstFile.c_str(), std::ios::binary);
        if (!in) {
            LOG_ERROR("open file {} error", srcFile);
            return;
        }
        if (!out) {
            LOG_ERROR("open file {} error", dstFile);
            return;
        }
        out << in.rdbuf();
        in.close();
        out.close();

        std::cout << "安装完成...[^^]" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // hide myself
        ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
#endif
    }

private:
#ifdef __linux__
#else
    BOOL killOtherProcessByName(const char *lpszProcessName) {
        unsigned int pid = -1;
        BOOL retval = TRUE;

        if (lpszProcessName == NULL) return -1;

        DWORD dwRet = 0;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 processInfo;
        processInfo.dwSize = sizeof(PROCESSENTRY32);
        int flag = Process32First(hSnapshot, &processInfo);

        // Find the process with name as same as lpszProcessName
        while (flag != 0) {
            if (strcmp(processInfo.szExeFile, lpszProcessName) == 0) {
                // Terminate the process.
                pid = processInfo.th32ProcessID;
                if (getpid() != pid) {
                    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, TRUE, pid);
                    if (TerminateProcess(hProcess, 0) != TRUE) { // Failed to terminate it.
                        retval = FALSE;
                        break;
                    }
                }
            }

            flag = Process32Next(hSnapshot, &processInfo);
        } // while (flag != 0)

        CloseHandle(hSnapshot);

        if (pid == -1) return FALSE;

        return retval;
    }

#endif
};
