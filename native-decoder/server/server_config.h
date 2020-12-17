#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <websocketpp/extensions/permessage_deflate/enabled.hpp>

namespace websocketpp {
namespace config {

/// Server config with asio transport and TLS disabled
struct ServerConfig : public websocketpp::config::asio {
    static const bool autonegotiate_compression = true;
    typedef websocketpp::extensions::permessage_deflate::enabled<permessage_deflate_config> permessage_deflate_type;
    static const int iothrnum = 4;
};
} // namespace config
} // namespace websocketpp