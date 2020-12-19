#include <memory>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>

#include "server/decode_server.h"

#include "common/helper/logger.h"
#include "common/helper/process_keeper.h"

#include "cmd/install.h"

int main(int argc, char *argv[]) {
    // init logger
    common::defaultLogger().init("native-decoder", "[%Y-%m-%d %H:%M:%S] [%P-%t] [%^%l%$] %v");

    // new process keeper
    auto keeper = common::ProcessKeeperFactory::newProcessKeeper();
    keeper->init(argc, argv);

    if (!keeper->isChild()) {
        InstallToolFactory::newInstallTool()->svcInstall(argv[0]);
    }

    // run app with keeper
    return keeper->run([](int c, char **v) {
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