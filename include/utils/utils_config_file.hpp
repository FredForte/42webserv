#ifndef UTILS_CONFIG_FILE_HPP
#define UTILS_CONFIG_FILE_HPP

#include "../../include/parser/ConfigTypes.hpp"
#include <string>

void printLocation(const LocationConfig& location);
std::string readFile(const std::string& path);
void printServer(const ServerConfig& server);
bool findStringOnVector(std::vector<std::string> vector, std::string toFind);
LocationConfig* findRequestedLocation(ServerConfig &server_conf, HttpRequest &request);
std::string getErrorPage(int code, ServerConfig& server);
std::string getContentType(const std::string& path);
std::string getServerSignature();
std::string determineConnection(const HttpRequest& request);
bool isCloseConnection(const std::string& connection);
HttpResponse getResponseMessage(int code, ServerConfig &server, LocationConfig responseLocation, const HttpRequest& request);
std::string parseResponseToOutPut(HttpResponse response);

#endif
