#include "../../include/parser/HttpRequestParser.hpp"

#include <cctype>
#include <cstdlib>

std::string HttpRequestParser::toLower(const std::string& s) {
    std::string result = s;
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    }
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
    while (value_start < line.size()
           && std::isspace(static_cast<unsigned char>(line[value_start]))) {
        value_start++;
    }

    size_t value_end = line.size();
    while (value_end > value_start
           && std::isspace(static_cast<unsigned char>(line[value_end - 1]))) {
        value_end--;
    }

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

        if (chunk_size == 0) {
            break;
        }

        request.body.append(raw, pos, chunk_size);
        pos += chunk_size + 2; // skip the chunk's data and its trailing CRLF (\r\n)
    }
}

// Content-Length is read straight off;
// chunked bodies get reassembled into one contiguous string here so nothing downstream ever
// has to deal with chunk framing.
void HttpRequestParser::parseBody(const std::string& raw, size_t body_start, HttpRequest& request) {
    std::map<std::string, std::string>::const_iterator encoding =
        request.headers.find("transfer-encoding");
    if (encoding != request.headers.end() && toLower(encoding->second) == "chunked") {
        parseChunkedBody(raw, body_start, request);
        return;
    }

    std::map<std::string, std::string>::const_iterator length =
        request.headers.find("content-length");
    if (length != request.headers.end()) {
        size_t body_length = static_cast<size_t>(std::atol(length->second.c_str()));
        // assign() copies the range straight in, better that substr for performance.
        request.body.assign(raw, body_start, body_length);
    }
}

// considers the raw requst is complete and updates the `pos` to point
// where the body starts.
// then parses the header into request object.
void HttpRequestParser::parseHeaderBlock(const std::string& raw, size_t& pos,
                                         HttpRequest& request) {
    while (pos < raw.size()) {
        size_t line_end = raw.find("\r\n", pos);
        std::string line = raw.substr(pos, line_end - pos);
        pos = line_end + 2; // advance past this line before checking - the
                            // blank line's own CRLF must be consumed too,
                            // so `pos` is correct when we break below
        if (line.empty()) {
            break;
        }
        parseHeaderLine(line, request);
    }
}

void HttpRequestParser::parseInto(const std::string& raw, HttpRequest& out) {
    // clear whatever the previous request on this connection left behind,
    // in case something got past our after response clearing.
    // body is released, because using only clear() keeps the allocation,
    // and that locks the size used before.
    out.method.clear();
    out.path.clear();
    out.query_string.clear();
    out.version.clear();
    out.headers.clear();
    std::string().swap(out.body);

    size_t pos = 0;
    while (pos + 1 < raw.size() && raw[pos] == '\r' && raw[pos + 1] == '\n') {
        pos += 2;
    }
    size_t line_end = raw.find("\r\n", pos);
    parseRequestLine(raw.substr(pos, line_end - pos), out);
    pos = line_end + 2;

    parseHeaderBlock(raw, pos, out);
    parseBody(raw, pos, out);
}

HttpRequest HttpRequestParser::parse(const std::string& raw) {
    HttpRequest request;
    parseInto(raw, request);
    return request;
}

// walks a chunked body, and treats running out of buffered byes as not done yet,
// can be safely used for the receiving end.
size_t HttpRequestParser::chunkedRequestLength(const std::string& buffer, RequestFraming& state) {
    // uses the saved state to continue on reading
    size_t pos = state.chunk_scan;

    while (true) {
        size_t line_end = buffer.find("\r\n", pos);
        if (line_end == std::string::npos) {
            state.chunk_scan = pos;
            return std::string::npos; // chunk-size line not fully received yet
        }

        size_t chunk_size = std::strtoul(buffer.substr(pos, line_end - pos).c_str(), NULL, 16);
        size_t data_start = line_end + 2;

        if (chunk_size == 0) {
            state.chunk_scan = data_start;
            return data_start; // terminating "0" chunk found - request is complete
        }

        if (buffer.size() < data_start + chunk_size + 2) {
            // saving the position read for future reads.
            state.chunk_scan = pos;
            return std::string::npos; // this chunk's data hasn't fully arrived yet
        }

        pos = data_start + chunk_size + 2; // skip the chunk's data and its trailing CRLF
    }
}

size_t HttpRequestParser::completeRequestLength(const std::string& buffer, RequestFraming& state) {
    // locate and measure the header block, set state when done with header.
    if (!state.headers_done) {
        size_t terminator = buffer.find("\r\n\r\n", state.header_scan);
        if (terminator == std::string::npos) {
            // ask: save the reading size for the next scan just before the tail,
            // so a terminator split across two chunks is still caught
            // without re-reading the front.
            state.header_scan = (buffer.size() > 3) ? buffer.size() - 3 : 0;
            return std::string::npos; // headers not fully received yet
        }

        HttpRequest probe;
        size_t pos = 0;
        while (pos + 1 < buffer.size() && buffer[pos] == '\r' && buffer[pos + 1] == '\n') {
            pos += 2;
        }
        pos = buffer.find("\r\n", pos) + 2;
        parseHeaderBlock(buffer, pos, probe);

        state.headers_done = true;
        state.header_length = pos;
        state.chunk_scan = pos;

        std::map<std::string, std::string>::const_iterator encoding =
            probe.headers.find("transfer-encoding");
        state.chunked = (encoding != probe.headers.end() && toLower(encoding->second) == "chunked");

        std::map<std::string, std::string>::const_iterator length =
            probe.headers.find("content-length");
        if (length != probe.headers.end()) {
            state.has_body_length = true;
            state.content_length = static_cast<size_t>(std::atol(length->second.c_str()));
        }
    }

    // with state saved, every later chunk is a size comparison.
    if (state.chunked) {
        return chunkedRequestLength(buffer, state);
    }

    if (state.has_body_length) {
        size_t total = state.header_length + state.content_length;
        return (buffer.size() >= total) ? total : std::string::npos;
    }

    return state.header_length; // no body at all - request ends after the headers
}
