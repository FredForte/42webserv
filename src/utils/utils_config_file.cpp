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
	std::cout << "Looking for requested location: " << request.path << std::endl;
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

// Returns the substring after the last '.' in the file's basename (not the
// whole path, so a dot in a directory name like "my.folder/file" is
// ignored), lowercased so ".JPG" and ".jpg" resolve to the same MIME type.
static std::string getFileExtension(const std::string& path) {
	size_t last_slash = path.find_last_of('/');
	std::string basename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

	size_t last_dot = basename.find_last_of('.');
	if (last_dot == std::string::npos)
		return "";

	std::string extension = basename.substr(last_dot + 1);
	for (size_t i = 0; i < extension.size(); i++)
		extension[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(extension[i])));

	return extension;
}

// Maps a served file's extension to its MIME type. Text-based types get
// "; charset=UTF-8" appended (meaningful for how they render); binary types
// (images, PDF) don't, since charset doesn't apply to them. Anything not in
// the table falls back to application/octet-stream - the standard "don't
// try to render this" signal, safer than guessing text/html for a binary
// file we don't recognize.
std::string getContentType(const std::string& path) {
	std::string extension = getFileExtension(path);

	std::map<std::string, std::string> mime_types;
	mime_types["html"] = "text/html";
	mime_types["htm"] = "text/html";
	mime_types["css"] = "text/css";
	mime_types["js"] = "application/javascript";
	mime_types["json"] = "application/json";
	mime_types["txt"] = "text/plain";
	mime_types["png"] = "image/png";
	mime_types["jpg"] = "image/jpeg";
	mime_types["jpeg"] = "image/jpeg";
	mime_types["gif"] = "image/gif";
	mime_types["svg"] = "image/svg+xml";
	mime_types["ico"] = "image/x-icon";
	mime_types["pdf"] = "application/pdf";

	std::map<std::string, std::string>::const_iterator it = mime_types.find(extension);
	std::string mime_type = (it != mime_types.end()) ? it->second : "application/octet-stream";

	bool is_text_based = mime_type.compare(0, 5, "text/") == 0
		|| mime_type == "application/javascript" || mime_type == "application/json";

	if (is_text_based)
		return mime_type + "; charset=UTF-8";

	return mime_type;
}

// Decides the Connection header value for a response. HTTP/1.1 defaults to
// keep-alive and HTTP/1.0 defaults to close, but the client's own Connection
// request header always overrides that default (RFC 7230 6.3).
std::string determineConnection(const HttpRequest& request) {
	std::map<std::string, std::string>::const_iterator it = request.headers.find("connection");
	std::string requested;
	if (it != request.headers.end()) {
		requested = it->second;
		for (size_t i = 0; i < requested.size(); i++)
			requested[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(requested[i])));
	}

	if (request.version == "HTTP/1.0")
		return requested == "keep-alive" ? "keep-alive" : "close";

	return requested == "close" ? "close" : "keep-alive";
}

// Single source of truth for reading a response's connection decision back
// out, so callers (e.g. the epoll loop deciding whether to close the socket
// once output_buffer drains) don't compare against the "close" literal themselves.
bool isCloseConnection(const std::string& connection) {
	return connection == "close";
}

// Here i am creating a page from scratch if its not found on the error pages path
// using the code and description on the http codes control object
// Not static: shared by the per-method handlers in response_handlers.cpp so every
// method reports errors (403/404/500/...) the same way.
std::string getErrorPage(int code, ServerConfig& server) {
	std::map<int, std::string>::const_iterator it = server.error_pages.find(code);
	if (it != server.error_pages.end()) {
		std::string custom_path = "www" + it->second;
		struct stat st;
		if (stat(custom_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
			std::ifstream file(custom_path.c_str());
			if (file.is_open()) {
				std::stringstream buffer;
				buffer << file.rdbuf();
				return buffer.str();
			}
		}
	}

	HttpResponseCodesIndex codesIndex;
	std::string desc = codesIndex.getDescription(code);

	std::stringstream ss;
	ss << "<html>\n<head><title>" << code << " " << desc << "</title></head>\n"
	   << "<body>\n<center><h1>" << code << " " << desc << "</h1></center>\n"
	   << "<hr><center>Webserv/1.0</center>\n</body>\n</html>";
	return ss.str();
}

// This is a crucial centralizing function for our responses.
// code variable here is a precheck_status
// If the caller already determined an error code
// we hand it straight to the error-page path.
// Otherwise we dispatch on the request method: each method owns its own
// success/error handling (GET reads files/autoindex, POST writes uploads,
// DELETE removes files) in response_handlers.cpp, so this function stays a router
// That handle pre-flight internal code control
// handlePostRequest will further define the right return codes for the responses that
// are dispatched by this router.
HttpResponse getResponseMessage(int code, ServerConfig &server, LocationConfig responseLocation, const HttpRequest& request) {
	HttpResponseCodesIndex codesIndex;

	if (code != 200) {
		HttpResponse response;
		response.server_name = server.server_name;
		response.connection = determineConnection(request);
		response.content_type = "text/html; charset=UTF-8";
		response.code = code;
		response.description = codesIndex.getDescription(code);
		response.body = getErrorPage(code, server);
		response.content_length = response.body.size();
		return response;
	}

	if (request.method == "GET")
		return handleGetRequest(server, responseLocation, request);
	if (request.method == "POST")
		return handlePostRequest(server, responseLocation, request);
	if (request.method == "DELETE")
		return handleDeleteRequest(server, responseLocation, request);

	HttpResponse response;
	response.server_name = server.server_name;
	response.connection = determineConnection(request);
	response.content_type = "text/html; charset=UTF-8";
	response.code = 405;
	response.description = codesIndex.getDescription(405);
	response.body = getErrorPage(405, server);
	response.content_length = response.body.size();
	return response;
}

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
