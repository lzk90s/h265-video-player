#include <memory>
#include <iostream>

#include "common/helper/wdt.h"
#include "common/helper/timer.h"

#include "server/decode_server.h"

decoder::DecodeServer server;

class Application {
public:
    Application(std::shared_ptr<common::Watchdog> wdt) : wdt_(wdt) {}

    int run(int argc, char **argv) {
        // start watchdog timer
        kickTimer_.StartTimer(1000, [=]() { wdt_->kick(); });

        uint16_t port = 9002;

        if (argc >= 2) {
            int i = atoi(argv[1]);
            if (i <= 0 || i > 65535) {
                std::cout << "invalid port" << std::endl;
                return 1;
            }
            port = uint16_t(i);
        }

        server.run(port);
        server.stop();

        return 0;
    }

private:
    std::shared_ptr<common::Watchdog> wdt_;
    common::Timer kickTimer_;
};

int main(int argc, char *argv[]) {
    std::shared_ptr<common::Watchdog> wdt(new common::Watchdog(5));
    Application w(wdt);
    wdt->start();
    return w.run(argc, argv);
}