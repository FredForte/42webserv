#include "../../include/parser/HttpRequestParser.hpp"

#include <cctype>
#include <cstdlib>

std::string HttpRequestParser::toLower(const std::string& s) {
	std::string result = s;
	for (size_t i = 0; i < result.size(); i++)
		result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
	return result;
}

// "GET /index.html?id=3 HTTP/1.1" -> method / target / version, then target
// splits on '?' into path / query_string.
void HttpRequestParser::parseRequestLine(const std::string& line, HttpRequest& request) {
	size_t first_space = line.find(' ');
	size_t second_space = line.find(' ', first_space + 1);

	request.method = line.substr(0, first_space);
	std::string target = line.substr(first_space + 1, second_space - first_space - 1);
	request.version = line.substr(second_space + 1);

	size_t query_pos = target.find('?');
	if (query_pos == std::string::npos) {
		request.path = target;
	} else {
		request.path = target.substr(0, query_pos);
		request.query_string = target.substr(query_pos + 1);
	}
}

// "Content-Length: 27" -> split on the first ':', trim surrounding
// whitespace off the value, store the name lowercase (header names are
// case-insensitive per RFC 7230).
void HttpRequestParser::parseHeaderLine(const std::string& line, HttpRequest& request) {
	size_t colon = line.find(':');
	std::string name = toLower(line.substr(0, colon));

	size_t value_start = colon + 1;
	while (value_start < line.size() && std::isspace(static_cast<unsigned char>(line[value_start])))
		value_start++;

	size_t value_end = line.size();
	while (value_end > value_start && std::isspace(static_cast<unsigned char>(line[value_end - 1])))
		value_end--;

	request.headers[name] = line.substr(value_start, value_end - value_start);
}

// Chunked transfer encoding: repeating "<hex-size>[;ext]\r\n<data>\r\n",
// terminated by a "0\r\n" chunk. strtoul() stops at the first non-hex
// character, so chunk extensions after ';' are skipped for free.
// Trailer headers after the terminating chunk aren't handled yet.
void HttpRequestParser::parseChunkedBody(const std::string& raw, size_t pos, HttpRequest& request) {
	while (pos < raw.size()) {
		size_t line_end = raw.find("\r\n", pos);
		std::string size_line = raw.substr(pos, line_end - pos);
		size_t chunk_size = std::strtoul(size_line.c_str(), NULL, 16);
		pos = line_end + 2;

		if (chunk_size == 0)
			break;

		request.body.append(raw, pos, chunk_size);
		pos += chunk_size + 2; // skip the chunk's data and its trailing CRLF (\r\n)
	}
}

// Content-Length is read straight off; 
//chunked bodies get reassembled into one contiguous string here so nothing downstream ever 
// has to deal with chunk framing.
void HttpRequestParser::parseBody(const std::string& raw, size_t body_start, HttpRequest& request) {
	std::map<std::string, std::string>::const_iterator encoding =
	    request.headers.find("transfer-encoding");
	if (encoding != request.headers.end() && toLower(encoding->second) == "chunked") {
		parseChunkedBody(raw, body_start, request);
		return;
	}

	std::map<std::string, std::string>::const_iterator length = request.headers.find("content-length");
	if (length != request.headers.end()) {
		size_t body_length = static_cast<size_t>(std::atol(length->second.c_str()));
		request.body = raw.substr(body_start, body_length);
	}
}

// Reads header lines from `pos` until the blank line, updating `pos` (by
// reference) to point just past it, where the body starts. 
// Safe to call whenever the header block is known to be fully present: either
// because `parse()`'s caller already guaranteed the whole request is
// buffered, or because completeRequestLength() only calls this after
// confirming the blank line was found.
void HttpRequestParser::parseHeaderBlock(const std::string& raw, size_t& pos, HttpRequest& request) {
	while (pos < raw.size()) {
		size_t line_end = raw.find("\r\n", pos);
		std::string line = raw.substr(pos, line_end - pos);
		pos = line_end + 2; // advance past this line before checking - the
		                     // blank line's own CRLF must be consumed too,
		                     // so `pos` is correct when we break below
		if (line.empty())
			break;
		parseHeaderLine(line, request);
	}
}

HttpRequest HttpRequestParser::parse(const std::string& raw) {
	HttpRequest request;

	size_t pos = 0;
	size_t line_end = raw.find("\r\n", pos);
	parseRequestLine(raw.substr(pos, line_end - pos), request);
	pos = line_end + 2;

	parseHeaderBlock(raw, pos, request);
	parseBody(raw, pos, request);

	return request;
}

// Walks a chunked body the same way parseChunkedBody() does, but treats
// running out of buffered bytes as "not complete yet" instead of assuming
// the data is there - that's the only difference from the happy-path
// version, and it's the part that actually has to be defensive, since
// finding the header-ending blank line says nothing about whether the body
// bytes after it have fully arrived.
size_t HttpRequestParser::chunkedRequestLength(const std::string& buffer, size_t pos) {
	while (true) {
		size_t line_end = buffer.find("\r\n", pos);
		if (line_end == std::string::npos)
			return std::string::npos; // chunk-size line not fully received yet

		size_t chunk_size = std::strtoul(buffer.substr(pos, line_end - pos).c_str(), NULL, 16);
		pos = line_end + 2;

		if (chunk_size == 0)
			return pos; // terminating "0" chunk found - request is complete

		if (buffer.size() < pos + chunk_size + 2)
			return std::string::npos; // this chunk's data hasn't fully arrived yet

		pos += chunk_size + 2; // skip the chunk's data and its trailing CRLF
	}
}

size_t HttpRequestParser::completeRequestLength(const std::string& buffer) {
	if (buffer.find("\r\n\r\n") == std::string::npos)
		return std::string::npos; // headers not fully received yet

	// The headers are now guaranteed complete, so it's safe to reuse the
	// same non-defensive header parsing parse() uses - no npos risk in this
	// region specifically, only in the body that follows it.
	HttpRequest probe;
	size_t pos = buffer.find("\r\n") + 2; // skip past the request-line
	parseHeaderBlock(buffer, pos, probe);

	std::map<std::string, std::string>::const_iterator encoding = probe.headers.find("transfer-encoding");
	if (encoding != probe.headers.end() && toLower(encoding->second) == "chunked")
		return chunkedRequestLength(buffer, pos);

	std::map<std::string, std::string>::const_iterator length = probe.headers.find("content-length");
	if (length != probe.headers.end()) {
		size_t total = pos + static_cast<size_t>(std::atol(length->second.c_str()));
		return (buffer.size() >= total) ? total : std::string::npos;
	}

	return pos; // no body at all - the request ends right after the headers
}
