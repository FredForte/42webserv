#ifndef HTTP_REQUEST_PARSER_HPP
#define HTTP_REQUEST_PARSER_HPP

#include "../../include/parser/HttpRequest.hpp"
#include <cstddef>

// a save state for a request, this prevents re-scanning behaviours and
// favors better performance for large sized chunked requests.
struct RequestFraming {
    bool headers_done;     // header block fully received and measured
    size_t header_length;  // header bytes including the blank line
    bool chunked;          // Transfer-Encoding: chunked
    bool has_body_length;  // a Content-Length header was present
    size_t content_length; // used when has_body_length true
    size_t header_scan;    // where to resume looking for the blank line
    size_t chunk_scan;     // where the chunked walk stopped, so it resumes there

    RequestFraming() {
        reset();
    }

    void reset() {
        headers_done = false;
        header_length = 0;
        chunked = false;
        has_body_length = false;
        content_length = 0;
        header_scan = 0;
        chunk_scan = 0;
    }
};

class HttpRequestParser {
public:
    // parses a full request, and assumes raw holds one complete request
    // completeRequestLength() needs to be called first to know when raw is done.
    HttpRequest parse(const std::string& raw);

    // added feature to fill an already existing request, this reduces
    // unecessary copies that can affect performance.
    void parseInto(const std::string& raw, HttpRequest& out);

    // tells the socket layer if it has buffered a full request.
    // returns std::string::npos if buffer is not a full request yet,
    // and keeps the accounted state for future reads.
    // if raw is full, returns the size read.
    size_t completeRequestLength(const std::string& buffer, RequestFraming& state);

private:
    void parseRequestLine(const std::string& line, HttpRequest& request);
    void parseHeaderLine(const std::string& line, HttpRequest& request);
    void parseHeaderBlock(const std::string& raw, size_t& pos, HttpRequest& request);
    void parseBody(const std::string& raw, size_t body_start, HttpRequest& request);
    void parseChunkedBody(const std::string& raw, size_t pos, HttpRequest& request);

    // state saving parseChunkedBody(), to be used in the receiving end.
    size_t chunkedRequestLength(const std::string& buffer, RequestFraming& state);

    static std::string toLower(const std::string& s);
};

#endif
