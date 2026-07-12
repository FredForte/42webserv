#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <map>
#include <string>

struct HttpRequest {
	std::string method;							// GET / POST
	std::string path;							// request location target
	std::string query_string;					// everything after '?', empty if none
	std::string version;						// HTTP/1.1
	std::map<std::string, std::string> headers;	// header names stored lowercase
	std::string body;
};

#endif
