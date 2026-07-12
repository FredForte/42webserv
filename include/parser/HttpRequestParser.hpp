#ifndef HTTP_REQUEST_PARSER_HPP
#define HTTP_REQUEST_PARSER_HPP

#include "../../include/parser/HttpRequest.hpp"

class HttpRequestParser {
public:
	// Parses one full request (request-line + headers + body) out of `raw`.
	// Happy path only: assumes `raw` holds exactly one complete request.
	// Call completeRequestLength() first to know when that's actually true.
	HttpRequest parse(const std::string& raw);

	// Tells the socket layer whether it has buffered a full request yet.
	// Returns std::string::npos if `buffer` doesn't hold one full request
	// yet (keep reading more bytes from the connection). Otherwise returns
	// the exact byte length of that one request within `buffer`, so the
	// caller can slice it off - e.g. parser.parse(buffer.substr(0, length))
	// - then buffer.erase(0, length) to drop those bytes and keep whatever
	// is left over (the start of the next pipelined/keep-alive request, if
	// any) for the next call.
	size_t completeRequestLength(const std::string& buffer);

private:
	void parseRequestLine(const std::string& line, HttpRequest& request);
	void parseHeaderLine(const std::string& line, HttpRequest& request);
	void parseHeaderBlock(const std::string& raw, size_t& pos, HttpRequest& request);
	void parseBody(const std::string& raw, size_t body_start, HttpRequest& request);
	void parseChunkedBody(const std::string& raw, size_t pos, HttpRequest& request);

	// Defensive counterpart to parseChunkedBody(): walks the same chunk
	// sequence but bails out with npos the moment a chunk-size line or a
	// chunk's data isn't fully buffered yet, instead of assuming it's there.
	size_t chunkedRequestLength(const std::string& buffer, size_t pos);

	static std::string toLower(const std::string& s);
};

#endif
