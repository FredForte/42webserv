#ifndef UTILS_CONFIG_FILE_HPP
#define UTILS_CONFIG_FILE_HPP

#include "../../include/parser/ConfigTypes.hpp"
#include <string>

void printLocation(const LocationConfig& location);
std::string readFile(const std::string& path);
void printServer(const ServerConfig& server);
bool findStringOnVector(std::vector<std::string> vector, std::string toFind);
LocationConfig* findRequestedLocation(ServerConfig &server_conf, HttpRequest &request);
HttpResponse getResponseMessage(int code, ServerConfig* server, LocationConfig responseLocation, const HttpRequest& request);
std::string determineConnection(const HttpRequest& request);
bool isCloseConnection(const std::string& connection);
std::string getContentType(const std::string& path);
std::string getErrorPage(int code, ServerConfig& server);
std::string getServerSignature();
std::vector<std::string> buildCgiEnv(const HttpRequest& request, const ServerConfig& server);
HttpResponse parseCgiResponse(const std::string& cgi_output, ServerConfig& server, const HttpRequest& request);
std::string parseResponseToOutPut(HttpResponse response);
std::string getFileExtension(const std::string& path);

#endif
