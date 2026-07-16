#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

// HTTP/1.1 200 OK\r\n
// Server: Webserv/1.0\r\n
// Date: Fri, 10 Jul 2026 00:56:58 GMT\r\n
// Content-Type: text/html; charset=UTF-8\r\n
// Content-Length: 48\r\n
// Connection: close\r\n
// \r\n
// <html><body><h1>Hello from Webserv!</h1></body></html>

#include <string>

struct HttpResponse {
	int code;						// http response code number
	std::string description;		// http response code description
	std::string server_name;		// server name that should come from the Config vector obj
	std::string date_time;			// date from function: 
	std::string content_type;		// todo: look into where to grab this type of response
	int content_length;				// size of the buffer that is going to be sent
	std::string connection;			// type of connecition, we should make an enum
	std::string body;				// should come from the response buffer
	std::string redirect_location;		// Location header value; empty means the header is omitted
};

#endif
