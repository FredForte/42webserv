#include "../../include/response/HttpResponseCodesIndex.hpp"

HttpResponseCodesIndex::HttpResponseCodesIndex() {
    // 2xx Success
    responseCodesDescriptions[200] = "OK";
    responseCodesDescriptions[201] = "Created";
    responseCodesDescriptions[202] = "Accepted";
    responseCodesDescriptions[204] = "No Content";

    // 3xx Redirection
    responseCodesDescriptions[301] = "Moved Permanently";
    responseCodesDescriptions[302] = "Found";
    responseCodesDescriptions[303] = "See Other";
    responseCodesDescriptions[304] = "Not Modified";
    responseCodesDescriptions[307] = "Temporary Redirect";
    responseCodesDescriptions[308] = "Permanent Redirect";

    // 4xx Client Error
    responseCodesDescriptions[400] = "Bad Request";
    responseCodesDescriptions[401] = "Unauthorized";
    responseCodesDescriptions[403] = "Forbidden";
    responseCodesDescriptions[404] = "Not Found";
    responseCodesDescriptions[405] = "Method Not Allowed";
    responseCodesDescriptions[408] = "Request Timeout";
    responseCodesDescriptions[409] = "Conflict";
    responseCodesDescriptions[413] = "Payload Too Large";
    responseCodesDescriptions[414] = "URI Too Long";
    responseCodesDescriptions[415] = "Unsupported Media Type";

    // 5xx Server Error
    responseCodesDescriptions[500] = "Internal Server Error";
    responseCodesDescriptions[501] = "Not Implemented";
    responseCodesDescriptions[502] = "Bad Gateway";
    responseCodesDescriptions[503] = "Service Unavailable";
    responseCodesDescriptions[504] = "Gateway Timeout";
    responseCodesDescriptions[505] = "HTTP Version Not Supported";
}

std::string HttpResponseCodesIndex::getDescription(int code) const {
    std::map<int, std::string>::const_iterator descriptionIndex =
        responseCodesDescriptions.find(code);
    if (descriptionIndex != responseCodesDescriptions.end()) {
        return descriptionIndex->second;
    }
    return "Error getting description";
}
