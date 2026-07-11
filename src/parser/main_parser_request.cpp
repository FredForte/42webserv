#include "ConfigParser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include "HttpRequestParser.hpp"
#include "../response/HttpResponse.hpp"
#include "../response/HttpResponseCodesIndex.hpp"

static std::string readFile(const std::string& path) {
	std::ifstream file(path.c_str());
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

static void headerMessagePrint(const std::string text) {
	int middle_line_half = (78 - text.size()) / 2;
	for (int i = 0; i < 80; i++)
		std::cout << "#";
	std::cout << "\n#";
	for (int i = 0; i < middle_line_half; i++)
		std::cout << " ";
	std::cout << text;
	for (int i = 0; i < middle_line_half; i++)
		std::cout << " ";
	std::cout << "#\n";
	for (int i = 0; i < 80; i++)
		std::cout << "#";
	std::cout << "\n";
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

// todo: reuse this function to know the location requested
LocationConfig* findRequestedLocation(ServerConfig &server_conf, HttpRequest &request) {
	std::cout << "Looking for requested location: " << request.path << std::endl;

	for (size_t i = 0; i < server_conf.locations.size(); i++) {
		if (server_conf.locations[i].path == request.path) {
			return &server_conf.locations[i];
		}
	}
	return NULL;
}

bool findStringOnVector(std::vector<std::string> vector, std::string toFind) {
	for (size_t i = 0; i < vector.size(); i++) {
		if (vector[i] == toFind)
			return true;
	}
	return false;
}

HttpResponse getResponseMessage(int code, ServerConfig &server, LocationConfig responseLocation) {
	HttpResponse response;
	response.code = code;

	HttpResponseCodesIndex codesIndex; // todo: make a class for the codes and description

	std::map<int, std::string>::iterator descriptionIndex = codesIndex.responseCodesDescriptions.find(code);
	if (descriptionIndex != codesIndex.responseCodesDescriptions.end()) {
		response.description = descriptionIndex->second;
	} else 
		response.description = "Error getting description";
	response.server_name = server.server_name;
	response.content_type = "Still need to figure this out"; // todo: figure this out

	std::string responseBody = readFile(responseLocation.root.append(responseLocation.index));
	response.content_length = responseBody.size();
	response.connection = "close"; // todo: get this info
	response.body = responseBody;

	return response;
}

std::string getHttpDateHeader() {
    // 1. Get current time
    std::time_t now = std::time(nullptr);
    
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

void parseResponseToOutPut(HttpResponse response) {
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

	output.append("Date: ");
	output.append(getHttpDateHeader());

	output.append("\r\n");

	output.append("Content-Type: text/html; charset=UTF-8");

	output.append("\r\n");

	std::stringstream ss2;
	ss2 << response.content_length;
	output.append("Content-Length: ");
	output.append(ss2.str());

	output.append("\r\n");

	output.append("Connection: close");

	output.append("\r\n");
	output.append("\r\n");

	output.append(response.body);

	std::cout << output << std::endl;
}

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "usage: " << argv[0] << " <config file> <raw http request_file>" << std::endl;
		return 1;
	}
	// config parser and info print
	headerMessagePrint("Server Configuration Parse");
	// std::cout << "Server Configuration Parse" << std::endl;
	std::string source = readFile(argv[1]);
	ConfigParser parser(source);
	Config config = parser.parse();
	for (size_t i = 0; i < config.size(); i++)
		printServer(config[i]);

	// request parser and print
	headerMessagePrint("Request Parse");
	// std::cout << "\n\nRequest Parse" << std::endl;
	std::string raw = readFile(argv[2]);
	HttpRequestParser req_parser;
		size_t full_length = req_parser.completeRequestLength(raw);
	std::cout << "completeRequestLength(full buffer): ";
	if (full_length == std::string::npos)
		std::cout << "npos (incomplete)\n";
	else
		std::cout << full_length << " (out of " << raw.size() << " buffered bytes)\n";
	if (full_length != std::string::npos && full_length > 1) {
		std::string truncated = raw.substr(0, full_length - 1);
		size_t truncated_length = req_parser.completeRequestLength(truncated);
		std::cout << "completeRequestLength(missing last byte of the request): ";
		if (truncated_length == std::string::npos)
			std::cout << "npos (incomplete, as expected)\n";
		else
			std::cout << truncated_length << " (unexpected: reported complete!)\n";
	}

	HttpRequest request = req_parser.parse(raw);

	std::cout << "method: " << request.method << "\n";
	std::cout << "path: " << request.path << "\n";
	std::cout << "query_string: " << request.query_string << "\n";
	std::cout << "version: " << request.version << "\n";
	std::cout << "headers:\n";
	for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
		it != request.headers.end(); ++it)
		std::cout << "  " << it->first << ": " << it->second << "\n";
	std::cout << "body (" << request.body.size() << " bytes): " << request.body << "\n";


	// Response
	headerMessagePrint("Response Info");
	LocationConfig *responseLocation = findRequestedLocation(config[0], request);
	if (responseLocation) {
		std::cout << "found the request location" << std::endl;
		std::cout << "Request method check on location" << std::endl;
		// check request method and location method
		if (findStringOnVector(responseLocation->methods, request.method)) {
			std::cout << "Found requested method: " << request.method << " on found location" << std::endl;
			if (request.method == "GET") {
				// create response for get method
				HttpResponse responseMessage = getResponseMessage(200, config[0], *responseLocation);
				headerMessagePrint("Response Message");
				parseResponseToOutPut(responseMessage);
			}
		} else 
			std::cout << "Method requested not allowed on location" << std::endl;

	} else 
		std::cout << "request location not found" << std::endl;


	
	return 0;
}