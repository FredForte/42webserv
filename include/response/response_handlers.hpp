#ifndef RESPONSE_HANDLERS_HPP
#define RESPONSE_HANDLERS_HPP

#include "../parser/ConfigTypes.hpp"
#include "../parser/HttpRequest.hpp"
#include "HttpResponse.hpp"

// One handler per HTTP method, each responsible for building the full
// HttpResponse (including error cases like 403/404) for its own method.
HttpResponse handleGetRequest(ServerConfig& server, LocationConfig& location, const HttpRequest& request);
HttpResponse handlePostRequest(ServerConfig& server, LocationConfig& location, const HttpRequest& request);
HttpResponse handleDeleteRequest(ServerConfig& server, LocationConfig& location, const HttpRequest& request);

#endif
