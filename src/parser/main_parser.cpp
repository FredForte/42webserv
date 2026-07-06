#include "ConfigParser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

static std::string readFile(const std::string& path) {
    std::ifstream file(path.c_str());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void printLocation(const LocationConfig& location) {
    std::cout << "  location " << location.path << " {\n";

    std::cout << "    methods:";
    for (size_t i = 0; i < location.methods.size(); i++)
        std::cout << " " << location.methods[i];
    std::cout << "\n";

    std::cout << "    root: " << location.root << "\n";
    std::cout << "    index: " << location.index << "\n";
    std::cout << "    autoindex: " << (location.autoindex ? "on" : "off") << "\n";
    std::cout << "    upload_enabled: " << (location.upload_enabled ? "true" : "false") << "\n";
    std::cout << "    upload_store: " << location.upload_store << "\n";
    std::cout << "    redirect_code: " << location.redirect_code << "\n";
    std::cout << "    redirect_target: " << location.redirect_target << "\n";

    std::cout << "    cgi_extensions:";
    for (std::map<std::string, std::string>::const_iterator it = location.cgi_extensions.begin();
         it != location.cgi_extensions.end(); ++it)
        std::cout << " " << it->first << "->" << it->second;
    std::cout << "\n";

    std::cout << "  }\n";
}

static void printServer(const ServerConfig& server) {
    std::cout << "server {\n";

    std::cout << "  listens:";
    for (size_t i = 0; i < server.listens.size(); i++)
        std::cout << " " << server.listens[i].host << ":" << server.listens[i].port;
    std::cout << "\n";

    std::cout << "  server_name: " << server.server_name << "\n";
    std::cout << "  client_max_body_size: " << server.client_max_body_size << "\n";

    std::cout << "  error_pages:";
    for (std::map<int, std::string>::const_iterator it = server.error_pages.begin();
         it != server.error_pages.end(); ++it)
        std::cout << " " << it->first << "->" << it->second;
    std::cout << "\n";

    for (size_t i = 0; i < server.locations.size(); i++)
        printLocation(server.locations[i]);

    std::cout << "}\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <config file>" << std::endl;
        return 1;
    }

    std::string source = readFile(argv[1]);
    ConfigParser parser(source);
    Config config = parser.parse();

    for (size_t i = 0; i < config.size(); i++)
        printServer(config[i]);

    return 0;
}