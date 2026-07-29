#include "../../include/parser/ConfigValidator.hpp"
#include "../../include/parser/ConfigTypes.hpp"
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

// Build a runtime_error whose message is the concatenation of the parts. Keeps
// the throw sites below readable without a printf-style format.
std::runtime_error configError(const std::string& message) {
    return std::runtime_error(std::string("config: ") + message);
}

std::string toString(int value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
}

} // namespace

// if the provided .conf file is invalid we block it here.
void ConfigValidator::validate(const Config& config) {
    if (config.empty()) {
        throw configError("no server blocks defined");
    }

    for (size_t i = 0; i < config.size(); i++) {
        validateServer(config[i]);
    }
}

void ConfigValidator::validateServer(const ServerConfig& serverConf) {
    // a server with nothing to listen on can never accept a connection.
    if (serverConf.listens.empty()) {
        throw configError("server has no \"listen\" directive");
    }

    // detect invalid port numbers.
    for (size_t i = 0; i < serverConf.listens.size(); i++) {
        int port = serverConf.listens[i].port;
        if (port < 1 || port > 65535) {
            throw configError("invalid listen port \"" + toString(port)
                              + "\" (must be 1-65535)");
        }
    }

    for (size_t i = 0; i < serverConf.listens.size(); i++) {
        for (size_t j = i + 1; j < serverConf.listens.size(); j++) {
            if (serverConf.listens[i].port == serverConf.listens[j].port) {
                throw configError("duplicate listen port \"" + toString(serverConf.listens[i].port) + "\"");
            }
        }
    }

    for (size_t i = 0; i < serverConf.locations.size(); i++) {
        validateLocation(serverConf.locations[i]);
    }

    // detect locations with the same path. 
    for (size_t i = 0; i < serverConf.locations.size(); i++) {
        for (size_t j = i + 1; j < serverConf.locations.size(); j++) {
            if (serverConf.locations[i].path == serverConf.locations[j].path) {
                throw configError("duplicate location \"" + serverConf.locations[i].path + "\"");
            }
        }
    }
}

void ConfigValidator::validateLocation(const LocationConfig& locationConf) {
    // detects invalid locations that do not start with relative '\'.
    if (locationConf.path.empty() || locationConf.path[0] != '/') {
        throw configError("location path \"" + locationConf.path + "\" must start with '/'");
    }

    // detect ivalid methods that are not GET, POST or DELETE.
    for (size_t i = 0; i < locationConf.methods.size(); i++) {
        const std::string& method = locationConf.methods[i];
        if (method != "GET" && method != "POST" && method != "DELETE") {
            throw configError("location \"" + locationConf.path + "\" lists unsupported method \""
                              + method + "\" (GET, POST, DELETE only)");
        }
    }

    // detects invalid redirection codes.
    if (locationConf.redirect_code != 0
        && (locationConf.redirect_code < 300 || locationConf.redirect_code > 399)) {
        throw configError("location \"" + locationConf.path + "\" return code "
                          + toString(locationConf.redirect_code) + " is not a 3xx redirect");
    }

    // A POST needs somewhere for the body to go: either a store to write it to or
    // a cgi to hand it to. Advertising POST without one is a config that can only
    // ever answer 403, so it is rejected at boot instead of at request time.
    // A "return" location is exempt: it answers every method with the redirect and
    // never looks at the body.
    bool allows_post = false;
    for (size_t i = 0; i < locationConf.methods.size(); i++) {
        if (locationConf.methods[i] == "POST") {
            allows_post = true;
            break;
        }
    }

    if (allows_post && locationConf.redirect_code == 0 && !locationConf.upload_enabled
        && locationConf.cgi_extensions.empty()) {
        throw configError("location \"" + locationConf.path
                          + "\" allows POST but has no \"upload_store\" to write to and no \"cgi\" "
                            "to hand the body to");
    }
}
