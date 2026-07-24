#ifndef RESPONSE_HANDLERS_HPP
#define RESPONSE_HANDLERS_HPP

#include "../client_connection.hpp"
#include "../parser/ConfigTypes.hpp"
#include "../parser/HttpRequest.hpp"
#include "HttpResponse.hpp"

// puts a finished response on the epollout for a client:
// serializes it into the output buffer, marks the connection ready,
// and arms EPOLLOUT so the event.
// main loop's write branch drains it.
void queue_response(int epoll_instance, client_connection_struct& client,
                    const HttpResponse& response);

// builds an error page for status_code and queues it by
// turning a per-request failure into a proper error response.
void queue_error_response(int epoll_instance, client_connection_struct& client, int status_code);

// One handler per HTTP method, each responsible for building the full
// HttpResponse (including error cases like 403/404) for its own method.
HttpResponse handleGetRequest(ServerConfig& server, LocationConfig& location,
                              const HttpRequest& request);
HttpResponse handlePostRequest(ServerConfig& server, LocationConfig& location,
                               const HttpRequest& request);
HttpResponse handleDeleteRequest(ServerConfig& server, LocationConfig& location,
                                 const HttpRequest& request);

// A location with redirect_code != 0 answers every method the same way, so this
// is checked ahead of (not dispatched alongside) the per-method handlers above.
HttpResponse buildRedirectResponse(ServerConfig& server, LocationConfig& location,
                                   const HttpRequest& request);
std::string joinPath(const std::string& a, const std::string& b);

#endif
