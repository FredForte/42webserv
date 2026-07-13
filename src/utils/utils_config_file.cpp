#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <map>
#include "../../include/parser/ConfigTypes.hpp"
#include "../../include/parser/HttpRequest.hpp"
#include "../../include/response/HttpResponse.hpp"
#include "../../include/response/HttpResponseCodesIndex.hpp"
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

// Here i am creating a page from scratch if its not found on the error pages path
// using the code and description on the http codes control object
static std::string getErrorPage(int code, ServerConfig& server) {
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

static std::string generateAutoIndexHtml(const std::string& directory_path, const std::string& uri_path) {
	DIR* dir = opendir(directory_path.c_str());
	if (!dir) {
		return "<html><body><h1>403 Forbidden</h1><p>Cannot open directory</p></body></html>";
	}

	std::stringstream html;
	html << "<html>\n<head><title>Index of " << uri_path << "</title></head>\n";
	html << "<body>\n<h1>Index of " << uri_path << "</h1>\n<hr>\n<pre>\n";

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		
		if (name == ".")
			continue;

		std::string link_path = uri_path;
		if (link_path.empty() || link_path[link_path.length() - 1] != '/') {
			link_path += "/";
		}
		link_path += name;

		std::string full_child_path = directory_path;
		if (full_child_path.empty() || full_child_path[full_child_path.length() - 1] != '/') {
			full_child_path += "/";
		}
		full_child_path += name;

		struct stat child_stat;
		std::string display_name = name;
		if (stat(full_child_path.c_str(), &child_stat) == 0 && S_ISDIR(child_stat.st_mode)) {
			display_name += "/";
			link_path += "/";
		}

		html << "<a href=\"" << link_path << "\">" << display_name << "</a>\n";
	}
	closedir(dir);

	html << "</pre>\n<hr>\n</body>\n</html>";
	return html.str();
}

// This is a crucial centralizing function for our responses
// Here we detect if the provided code is not 200 (for success) and start handing it for provide
// error pages, if an erro page is not found on our parsed server config object, we will create it
//
// If its a success we will analyze the requested path using a series of checks
// First we will consider '/' for an empty provided path and treat it as the root server location
// If we get a path we will then test it for the length of the location path for a match
// Then test it for directory, if its a directory we check for an index file, if not we check 
// for autoindex, if autoindex is enabled we will generate an autoindex page, if not we will return a 403
// If its not a directory we check for cgi, if cgi is enabled we will generate a cgi response
// if cgi is not enabled we will return a 404
HttpResponse getResponseMessage(int code, ServerConfig &server, LocationConfig responseLocation, const HttpRequest& request) {
	HttpResponse response;
	HttpResponseCodesIndex codesIndex;
	
	response.server_name = server.server_name;
	response.connection = "keep-alive";
	response.content_type = "text/html; charset=UTF-8";

	if (code != 200) {
		response.code = code;
		response.description = codesIndex.getDescription(code);
		response.body = getErrorPage(code, server);
		response.content_length = response.body.size();
		return response;
	}

	std::string root_path = responseLocation.root;
	std::string req_path = request.path;
	
	if (!root_path.empty() && root_path[root_path.length() - 1] == '/' && !req_path.empty() && req_path[0] == '/') {
		req_path = req_path.substr(1);
	} else if (!root_path.empty() && root_path[root_path.length() - 1] != '/' && !req_path.empty() && req_path[0] != '/') {
		root_path += "/";
	}
	std::string local_path = root_path + req_path;

	struct stat path_stat;
	if (stat(local_path.c_str(), &path_stat) == 0) {
		if (S_ISDIR(path_stat.st_mode)) {
			std::string index_path = local_path;
			if (index_path.empty() || index_path[index_path.length() - 1] != '/') {
				index_path += "/";
			}
			index_path += responseLocation.index;

			struct stat index_stat;
			if (stat(index_path.c_str(), &index_stat) == 0 && S_ISREG(index_stat.st_mode)) {
				response.code = 200;
				response.description = codesIndex.getDescription(200);
				response.body = readFile(index_path);
			} else {
				if (responseLocation.autoindex) {
					response.code = 200;
					response.description = codesIndex.getDescription(200);
					response.body = generateAutoIndexHtml(local_path, request.path);
				} else {
					response.code = 403;
					response.description = codesIndex.getDescription(403);
					response.body = getErrorPage(403, server);
				}
			}
		} 
		else if (S_ISREG(path_stat.st_mode)) {
			response.code = 200;
			response.description = codesIndex.getDescription(200);
			response.body = readFile(local_path);
		} 
		else {
			response.code = 403;
			response.description = codesIndex.getDescription(403);
			response.body = getErrorPage(403, server);
		}
	} else {
		response.code = 404;
		response.description = codesIndex.getDescription(404);
		response.body = getErrorPage(404, server);
	}

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

	output.append("Content-Type: text/html; charset=UTF-8");

	output.append("\r\n");

	std::stringstream ss2;
	ss2 << response.content_length;
	output.append("Content-Length: ");
	output.append(ss2.str());

	output.append("\r\n");

	output.append("Connection: close");

	output.append("\r\n"
				  "\r\n");

	output.append(response.body);

	return output;
}
