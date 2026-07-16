#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cerrno>
#include <map>
#include "../../include/parser/ConfigTypes.hpp"
#include "../../include/parser/HttpRequest.hpp"
#include "../../include/response/HttpResponse.hpp"
#include "../../include/response/HttpResponseCodesIndex.hpp"
#include "../../include/response/response_handlers.hpp"
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>

void printLocation(const LocationConfig& location) {
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

std::string readFile(const std::string& path) {
	std::ifstream file(path.c_str());
	std::stringstream buffer;

	buffer << file.rdbuf();

	return buffer.str();
}

void printServer(const ServerConfig& server) {
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

bool findStringOnVector(std::vector<std::string> vector, std::string toFind) {
	for (size_t i = 0; i < vector.size(); i++) {
		if (vector[i] == toFind)
			return true;
	}
	return false;
}

LocationConfig* findRequestedLocation(ServerConfig &server_conf, HttpRequest &request) {
	// std::cout << "Looking for requested location: " << request.path << std::endl;
	LocationConfig* best_match = NULL;
	size_t max_len = 0;

	for (size_t i = 0; i < server_conf.locations.size(); i++) {
		const std::string& loc_path = server_conf.locations[i].path;
		if (request.path.compare(0, loc_path.length(), loc_path) == 0) {
			if (loc_path.length() > max_len) {
				max_len = loc_path.length();
				best_match = &server_conf.locations[i];
			}
		}
	}
	return best_match;
}

HttpResponse getResponseMessage(int code, ServerConfig* server, LocationConfig responseLocation) {
	HttpResponse response;
	response.code = code;

	HttpResponseCodesIndex codesIndex; // todo: make a class for the codes and description

	response.description = codesIndex.getDescription(code);
	response.server_name = server->server_name;
	response.content_type = "Still need to figure this out"; // todo: figure this out

	std::string responseBody = readFile(responseLocation.root.append(responseLocation.index));
	response.content_length = responseBody.size() + 4;
	// response.connection = "close"; // todo: get this info
	response.connection = "keep-alive";
	response.body = responseBody;

	return response;
}

// Boiler-plate type of date and time cpp getter
// Already returning the full header line for Date:
// Returns empty if formatting fails
static std::string getHttpDateHeader() {
    // 1. Get current time
    std::time_t now = std::time(NULL);

    // 2. Convert to UTC (GMT) safely
    std::tm* gmt = std::gmtime(&now);

    // 3. Buffer to hold the formatted string
    char buffer[100];

    // %a = Abbreviated weekday (e.g., Fri)
    // %d = Day of the month (e.g., 10)
    // %b = Abbreviated month name (e.g., Jul)
    // %Y = Year (e.g., 2026)
    // %T = Time in 24-hour HH:MM:SS format
    if (std::strftime(buffer, sizeof(buffer), "Date: %a, %d %b %Y %T GMT", gmt)) {
        return std::string(buffer);
    }

    return ""; // Fallback if formatting fails
}

// Very procedural and illustrative way of creating our response
// header and body, I've left it spaced out as much as possible
// to make it clear where each header line goes and how its
// being set here
std::string parseResponseToOutPut(HttpResponse response) {
	std::string output;
	output.append("HTTP/1.1 ");

	std::stringstream ss;
	ss << response.code;
	output.append(ss.str());

	output.append(" ");
	output.append(response.description);

	output.append("\r\n");

	output.append("Server: ");
	output.append(response.server_name);

	output.append("\r\n");

	output.append(getHttpDateHeader());

	output.append("\r\n");

	output.append("Content-Type: ");
	output.append(response.content_type);

	output.append("\r\n");

	std::stringstream ss2;
	ss2 << response.content_length;
	output.append("Content-Length: ");
	output.append(ss2.str());

	output.append("\r\n");

	if (!response.redirect_location.empty()) {
		output.append("Location: ");
		output.append(response.redirect_location);
		output.append("\r\n");
	}

	output.append("Connection: ");
	output.append(response.connection);

	output.append("\r\n"
				  "\r\n");

	output.append(response.body);

	return output;
}
