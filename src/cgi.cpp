#include "string"

// Have a careful look at the environment variables involved in the web
// server-CGI communication. The full request and arguments provided by
// the client must be available to the CGI.

struct cgi_env_variables {
    std::string QUERY_STRING;
    std::string QUERY_STRING;
};
