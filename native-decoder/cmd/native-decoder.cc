#include <memory>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>

#include "server/decode_server.h"

#include "common/helper/logger.h"
#include "common/helper/process_keeper.h"

void svcInstall(const std::string &program) {
#ifdef __linux__
    // use systemd
#else
    auto srcFile = program;
    auto pos     = program.find_last_of("\\") == std::string::npos ? 0 : program.find_last_of("\\") + 1;
    auto exe     = program.substr(pos, program.length());
    auto dstFile = std::string(::getenv("APPDATA")) + "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\" + exe;
    if (srcFile == dstFile) {
        // 路径相同，说明是已经安装好的
        return;
    }

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

int main(int argc, char *argv[]) {
    // init logger
    common::defaultLogger().init("native-decoder", "[%Y-%m-%d %H:%M:%S] [%P-%t] [%^%l%$] %v");

    // new process keeper
    auto keeper = common::newProcessKeeper();
    keeper->init(argc, argv);

    if (!keeper->isChild()) {
        svcInstall(argv[0]);
    }

    // run app with keeper
    keeper->run([](int c, char **v) {
        LOG_INFO("---------------------------------------------------");
        LOG_INFO("Process id is {}", getpid());
        decoder::DecodeServer server;
        uint16_t port = 9002;
        if (c >= 2) {
            int i = atoi(v[1]);
            if (i <= 0 || i > 65535) {
                LOG_ERROR("invalid port");
                return -1;
            }
            port = uint16_t(i);
        }
        server.run(port);
        server.stop();
        return 0;
    });
}