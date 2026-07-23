#ifndef CONFIG_VALIDATOR_HPP
#define CONFIG_VALIDATOR_HPP

#include "ConfigTypes.hpp"

class ConfigValidator {
    public:
        void  validateServer(const ServerConfig& serverConf);
        void validateLocation(const LocationConfig& locationConf);
};

#endif