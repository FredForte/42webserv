#include "../../include/parser/HttpRequestParser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

static std::string readFile(const std::string& path) {
	std::ifstream file(path.c_str());
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: " << argv[0] << " <raw http request file>" << std::endl;
		return 1;
	}

	std::string raw = readFile(argv[1]);
	HttpRequestParser parser;

	size_t full_length = parser.completeRequestLength(raw);
	std::cout << "completeRequestLength(full buffer): ";
	if (full_length == std::string::npos)
		std::cout << "npos (incomplete)\n";
	else
		std::cout << full_length << " (out of " << raw.size() << " buffered bytes)\n";

	// Truncate relative to the request's own detected boundary, not the raw
	// file size - the file may have trailing bytes past the real request
	// (e.g. an editor-inserted final newline) that aren't part of it at all.
	if (full_length != std::string::npos && full_length > 1) {
		std::string truncated = raw.substr(0, full_length - 1);
		size_t truncated_length = parser.completeRequestLength(truncated);
		std::cout << "completeRequestLength(missing last byte of the request): ";
		if (truncated_length == std::string::npos)
			std::cout << "npos (incomplete, as expected)\n";
		else
			std::cout << truncated_length << " (unexpected: reported complete!)\n";
	}

	HttpRequest request = parser.parse(raw);

	std::cout << "method: " << request.method << "\n";
	std::cout << "path: " << request.path << "\n";
	std::cout << "query_string: " << request.query_string << "\n";
	std::cout << "version: " << request.version << "\n";
	std::cout << "headers:\n";
	for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
		it != request.headers.end(); ++it)
		std::cout << "  " << it->first << ": " << it->second << "\n";
	std::cout << "body (" << request.body.size() << " bytes): " << request.body << "\n";

	std::cout << "\n\nTesting Response to the requester" << std::endl;


	return 0;
}
