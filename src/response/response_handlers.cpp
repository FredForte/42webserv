#include "../../include/response/response_handlers.hpp"
#include "../../include/response/HttpResponseCodesIndex.hpp"
#include "../../include/utils/utils_config_file.hpp"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// Joins two path segments with exactly one '/' between them, regardless of
// whether either side already has one.
static std::string joinPath(const std::string& a, const std::string& b) {
	if (a.empty())
		return b;
	if (b.empty())
		return a;

	bool a_has_slash = a[a.length() - 1] == '/';
	bool b_has_slash = b[0] == '/';

	if (a_has_slash && b_has_slash)
		return a + b.substr(1);
	if (!a_has_slash && !b_has_slash)
		return a + "/" + b;
	return a + b;
}

// Extracts the last path segment of the request, relative to the location's
// prefix, to use as the uploaded file's name. Only the basename is kept
// (anything after the last '/') so a request path can never point the write
// outside of upload_store, even if it contains "..".
static std::string extractUploadFilename(const LocationConfig& location, const HttpRequest& request) {
	std::string relative = request.path;

	if (request.path.compare(0, location.path.length(), location.path) == 0)
		relative = request.path.substr(location.path.length());

	size_t last_slash = relative.find_last_of('/');
	std::string filename = (last_slash == std::string::npos) ? relative : relative.substr(last_slash + 1);

	if (filename.empty() || filename == "." || filename == "..")
		return "";

	return filename;
}

// First checks if the directory is accessible and exists, if not create a hardcoded 403 page
// Proceeds to generate the autoindex page
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

		std::string full_child_path = joinPath(directory_path, name);

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

// Called from getResponseMessage after selecting the methods
// Set default values for connection and content-type initially
// After detecting the path and testing it's type
// proceeds to get correct types if necessary
HttpResponse handleGetRequest(ServerConfig& server, LocationConfig& location, const HttpRequest& request) {
	HttpResponse response;
	HttpResponseCodesIndex codesIndex;

	response.server_name = server.server_name;
	response.connection = determineConnection(request);
	response.content_type = "text/html; charset=UTF-8";

	std::string local_path = joinPath(location.root, request.path);

	struct stat path_stat;
	if (stat(local_path.c_str(), &path_stat) == 0) {
		if (S_ISDIR(path_stat.st_mode)) {
			std::string index_path = joinPath(local_path, location.index);

			struct stat index_stat;
			if (stat(index_path.c_str(), &index_stat) == 0 && S_ISREG(index_stat.st_mode)) {
				response.code = 200;
				response.description = codesIndex.getDescription(200);
				response.body = readFile(index_path);
				response.content_type = getContentType(index_path);
			} else if (location.autoindex) {
				response.code = 200;
				response.description = codesIndex.getDescription(200);
				response.body = generateAutoIndexHtml(local_path, request.path);
			} else {
				response.code = 403;
				response.description = codesIndex.getDescription(403);
				response.body = getErrorPage(403, server);
			}
		} else if (S_ISREG(path_stat.st_mode)) {
			response.code = 200;
			response.description = codesIndex.getDescription(200);
			response.body = readFile(local_path);
			response.content_type = getContentType(local_path);
		} else {
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

HttpResponse handlePostRequest(ServerConfig& server, LocationConfig& location, const HttpRequest& request) {
	HttpResponse response;
	HttpResponseCodesIndex codesIndex;

	response.server_name = server.server_name;
	response.connection = determineConnection(request);
	response.content_type = "text/html; charset=UTF-8";

	if (!location.upload_enabled) {
		response.code = 403;
		response.description = codesIndex.getDescription(403);
		response.body = getErrorPage(403, server);
		response.content_length = response.body.size();
		return response;
	}

	std::string filename = extractUploadFilename(location, request);
	if (filename.empty()) {
		response.code = 400;
		response.description = codesIndex.getDescription(400);
		response.body = getErrorPage(400, server);
		response.content_length = response.body.size();
		return response;
	}

	std::string target_path = joinPath(location.upload_store, filename);

	std::ofstream out_file(target_path.c_str(), std::ios::binary | std::ios::trunc);
	if (!out_file.is_open()) {
		response.code = 500;
		response.description = codesIndex.getDescription(500);
		response.body = getErrorPage(500, server);
		response.content_length = response.body.size();
		return response;
	}

	out_file.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
	out_file.close();

	std::stringstream body;
	body << "<html>\n<head><title>201 Created</title></head>\n"
		 << "<body>\n<center><h1>201 Created</h1></center>\n"
		 << "<center>Uploaded to " << request.path << "</center>\n"
		 << "<hr><center>Webserv/1.0</center>\n</body>\n</html>";

	response.code = 201;
	response.description = codesIndex.getDescription(201);
	response.body = body.str();
	response.content_length = response.body.size();
	return response;
}

HttpResponse buildRedirectResponse(ServerConfig& server, LocationConfig& location, const HttpRequest& request) {
	HttpResponse response;
	HttpResponseCodesIndex codesIndex;

	response.server_name = server.server_name;
	response.connection = determineConnection(request);
	response.content_type = "text/html; charset=UTF-8";
	response.code = location.redirect_code;
	response.description = codesIndex.getDescription(location.redirect_code);
	response.redirect_location = location.redirect_target;

	std::stringstream body;
	body << "<html>\n<head><title>" << response.code << " " << response.description << "</title></head>\n"
		 << "<body>\n<center><h1>" << response.code << " " << response.description << "</h1></center>\n"
		 << "<center>Redirecting to <a href=\"" << location.redirect_target << "\">"
		 << location.redirect_target << "</a></center>\n"
		 << "<hr><center>Webserv/1.0</center>\n</body>\n</html>";

	response.body = body.str();
	response.content_length = response.body.size();
	return response;
}

HttpResponse handleDeleteRequest(ServerConfig& server, LocationConfig& location, const HttpRequest& request) {
	HttpResponse response;
	HttpResponseCodesIndex codesIndex;

	response.server_name = server.server_name;
	response.connection = determineConnection(request);
	response.content_type = "text/html; charset=UTF-8";

	std::string local_path = joinPath(location.root, request.path);

	struct stat path_stat;
	if (stat(local_path.c_str(), &path_stat) != 0) {
		response.code = 404;
		response.description = codesIndex.getDescription(404);
		response.body = getErrorPage(404, server);
		response.content_length = response.body.size();
		return response;
	}

	if (!S_ISREG(path_stat.st_mode)) {
		response.code = 403;
		response.description = codesIndex.getDescription(403);
		response.body = getErrorPage(403, server);
		response.content_length = response.body.size();
		return response;
	}

	if (unlink(local_path.c_str()) != 0) {
		response.code = 500;
		response.description = codesIndex.getDescription(500);
		response.body = getErrorPage(500, server);
		response.content_length = response.body.size();
		return response;
	}

	response.code = 204;
	response.description = codesIndex.getDescription(204);
	response.body = "";
	response.content_length = 0;
	return response;
}
