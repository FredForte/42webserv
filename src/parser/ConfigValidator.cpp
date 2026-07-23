#include "../../include/parser/ConfigValidator.hpp"
#include "../../include/parser/ConfigTypes.hpp"
#include <string>
#include <stdexcept>


void ConfigValidator::validateServer(const ServerConfig& serverConf) {
    if (serverConf.server_name.empty())
        throw std::runtime_error("Server name is empty");
    if (serverConf.listens.empty())
        throw std::runtime_error("No listen port provided");
    for (int i = 0; i < serverConf.locations.size(); i++)
        validateLocation(serverConf.locations[i]);
    
    // detect duplicated locations paths
    for (int i = 0; i < serverConf.locations.size(); i++) {
        for (int j = i + 1; j < serverConf.locations.size(); j++) {
            if (i == j)
                continue;
            if (serverConf.locations[i].path == serverConf.locations[j].path)
                throw std::runtime_error("Duplicated location set");
        }
     }

}

void ConfigValidator::validateLocation(const LocationConfig& locationConf) {

}
