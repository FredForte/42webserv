#ifndef UTILS_CONFIG_FILE_HPP
#define UTILS_CONFIG_FILE_HPP

#include "../../include/parser/ConfigTypes.hpp"
#include <string>

void printLocation(const LocationConfig& location);
std::string readFile(const std::string& path);
void printServer(const ServerConfig& server);
bool findStringOnVector(std::vector<std::string> vector, std::string toFind);
LocationConfig* findRequestedLocation(ServerConfig &server_conf, HttpRequest &request);
HttpResponse getResponseMessage(int code, ServerConfig &server, LocationConfig responseLocation);
std::string parseResponseToOutPut(HttpResponse response);

#endif
