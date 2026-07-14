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

// A location with redirect_code != 0 answers every method the same way, so this
// is checked ahead of (not dispatched alongside) the per-method handlers above.
HttpResponse buildRedirectResponse(ServerConfig& server, LocationConfig& location, const HttpRequest& request);

#endif
