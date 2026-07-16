#include "../../include/parser/ConfigParser.hpp"
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include "../../include/parser/HttpRequestParser.hpp"
#include "../../include/response/HttpResponse.hpp"
#include "../../include/response/HttpResponseCodesIndex.hpp"
#include "../../include/utils/utils_config_file.hpp"

static void headerMessagePrint(const std::string text) {
	int middle_line_half = (78 - text.size()) / 2;
	for (int i = 0; i < 80; i++)
		std::cout << "#";
	std::cout << "\n#";
	for (int i = 0; i < middle_line_half; i++)
		std::cout << " ";
	std::cout << text;
	for (int i = 0; i < middle_line_half; i++)
		std::cout << " ";
	std::cout << "#\n";
	for (int i = 0; i < 80; i++)
		std::cout << "#";
	std::cout << "\n";
}

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "usage: " << argv[0] << " <config file> <raw http request_file>" << std::endl;
		return 1;
	}
	// config parser and info print
	headerMessagePrint("Server Configuration Parse");
	// std::cout << "Server Configuration Parse" << std::endl;
	std::string source = readFile(argv[1]);
	ConfigParser parser(source);
	Config config = parser.parse();
	for (size_t i = 0; i < config.size(); i++)
		printServer(config[i]);

	// request parser and print
	headerMessagePrint("Request Parse");
	// std::cout << "\n\nRequest Parse" << std::endl;
	std::string raw = readFile(argv[2]);
	HttpRequestParser req_parser;
	size_t full_length = req_parser.completeRequestLength(raw);
	std::cout << "completeRequestLength(full buffer): ";
	if (full_length == std::string::npos)
		std::cout << "npos (incomplete)\n";
	else
		std::cout << full_length << " (out of " << raw.size() << " buffered bytes)\n";
	if (full_length != std::string::npos && full_length > 1) {
		std::string truncated = raw.substr(0, full_length - 1);
		size_t truncated_length = req_parser.completeRequestLength(truncated);
		std::cout << "completeRequestLength(missing last byte of the request): ";
		if (truncated_length == std::string::npos)
			std::cout << "npos (incomplete, as expected)\n";
		else
			std::cout << truncated_length << " (unexpected: reported complete!)\n";
	}

	HttpRequest request = req_parser.parse(raw);

	std::cout << "method: " << request.method << "\n";
	std::cout << "path: " << request.path << "\n";
	std::cout << "query_string: " << request.query_string << "\n";
	std::cout << "version: " << request.version << "\n";
	std::cout << "headers:\n";
	for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
		it != request.headers.end(); ++it)
		std::cout << "  " << it->first << ": " << it->second << "\n";
	std::cout << "body (" << request.body.size() << " bytes): " << request.body << "\n";


	// Response
	headerMessagePrint("Response Info");
	LocationConfig *responseLocation = findRequestedLocation(config[0], request);
	if (responseLocation) {
		std::cout << "found the request location" << std::endl;
		std::cout << "Request method check on location" << std::endl;
		// check request method and location method
		if (findStringOnVector(responseLocation->methods, request.method)) {
			std::cout << "Found requested method: " << request.method << " on found location" << std::endl;
			if (request.method == "GET") {
				// create response for get method
				HttpResponse responseMessage = getResponseMessage(200, config[0], *responseLocation, request);
				headerMessagePrint("Response Message");
				std::cout << parseResponseToOutPut(responseMessage) << std::endl;
			}
		} else
			std::cout << "Method requested not allowed on location" << std::endl;

	} else
		std::cout << "request location not found" << std::endl;



	return 0;
}
