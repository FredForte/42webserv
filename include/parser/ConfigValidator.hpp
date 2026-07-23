#ifndef CONFIG_VALIDATOR_HPP
#define CONFIG_VALIDATOR_HPP

#include "ConfigTypes.hpp"

// Rejects configurations that would leave the server in a broken or ambiguous
// state (e.g. no server blocks, a server with no port to listen on, an invalid
// port, or a location with an unusable path/method). Every problem is reported
// by throwing std::runtime_error, so main can catch it and exit with a message
// instead of starting a server that can never answer.
class ConfigValidator {
    public:
        // Top-level entry point: validates the whole parsed config.
        void validate(const Config& config);

        void validateServer(const ServerConfig& serverConf);
        void validateLocation(const LocationConfig& locationConf);
};

#endif
