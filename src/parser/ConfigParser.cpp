#include "ConfigParser.hpp"

#include <cstdlib>

ConfigParser::ConfigParser(const std::string& source) : _tokenizer(source) {}

std::string ConfigParser::expectWord() {
	return _tokenizer.next().value;
}

//todo: after happy path tested, add the check for the provided token type.
void ConfigParser::expect(TokenType type) {
	(void)type;
	_tokenizer.next();
}

Config ConfigParser::parse() {
	Config config;

	while (_tokenizer.peek().type != TOKEN_END) {
		expectWord(); // consumes the "server" keyword
		config.push_back(parseServer());
	}

	return config;
}

ServerConfig ConfigParser::parseServer() {
	ServerConfig server;
	server.client_max_body_size = 0;

	expect(TOKEN_LBRACE);

	while (_tokenizer.peek().type != TOKEN_RBRACE) {
		std::string directive = expectWord();

		if (directive == "listen")
			parseListen(server);
		else if (directive == "server_name")
			parseServerName(server);
		else if (directive == "error_page")
			parseErrorPage(server);
		else if (directive == "client_max_body_size")
			parseClientMaxBodySize(server);
		else if (directive == "location") {
			std::string path = expectWord();
			server.locations.push_back(parseLocation(path));
		}
	}

	expect(TOKEN_RBRACE);
	return server;
}

LocationConfig ConfigParser::parseLocation(const std::string& path) {
	LocationConfig location;
	location.path = path;
	location.autoindex = false;
	location.upload_enabled = false;
	location.redirect_code = 0;

	expect(TOKEN_LBRACE);

	while (_tokenizer.peek().type != TOKEN_RBRACE) {
		std::string directive = expectWord();

		if (directive == "methods")
			parseMethods(location);
		else if (directive == "root")
			parseRoot(location);
		else if (directive == "index")
			parseIndex(location);
		else if (directive == "autoindex")
			parseAutoindex(location);
		else if (directive == "upload_store")
			parseUploadStore(location);
		else if (directive == "return")
			parseReturn(location);
		else if (directive == "cgi")
			parseCgi(location);
	}

	expect(TOKEN_RBRACE);
	return location;
}

// forcng local and reading port
void ConfigParser::parseListen(ServerConfig& server) {
	std::string arg = expectWord();
	expect(TOKEN_SEMICOLON);

	ListenAddr addr;
	size_t colon = arg.find(':');
	if (colon == std::string::npos) {
		addr.host = "0.0.0.0";
		addr.port = std::atoi(arg.c_str());
	} else {
		addr.host = arg.substr(0, colon);
		addr.port = std::atoi(arg.substr(colon + 1).c_str());
	}
	server.listens.push_back(addr);
}

void ConfigParser::parseServerName(ServerConfig& server) {
	server.server_name = expectWord();
	expect(TOKEN_SEMICOLON);
}

void ConfigParser::parseErrorPage(ServerConfig& server) {
	std::string code = expectWord();
	std::string path = expectWord();
	expect(TOKEN_SEMICOLON);
	server.error_pages[std::atoi(code.c_str())] = path;
}

void ConfigParser::parseClientMaxBodySize(ServerConfig& server) {
	std::string value = expectWord();
	expect(TOKEN_SEMICOLON);
	server.client_max_body_size = static_cast<size_t>(std::atol(value.c_str()));
}

void ConfigParser::parseMethods(LocationConfig& location) {
	while (_tokenizer.peek().type == TOKEN_WORD)
		location.methods.push_back(_tokenizer.next().value);
	expect(TOKEN_SEMICOLON);
}

void ConfigParser::parseRoot(LocationConfig& location) {
	location.root = expectWord();
	expect(TOKEN_SEMICOLON);
}

void ConfigParser::parseIndex(LocationConfig& location) {
	location.index = expectWord();
	expect(TOKEN_SEMICOLON);
}

void ConfigParser::parseAutoindex(LocationConfig& location) {
	std::string value = expectWord();
	expect(TOKEN_SEMICOLON);
	location.autoindex = (value == "on");
}

void ConfigParser::parseUploadStore(LocationConfig& location) {
	location.upload_store = expectWord();
	location.upload_enabled = true;
	expect(TOKEN_SEMICOLON);
}

void ConfigParser::parseReturn(LocationConfig& location) {
	std::string code = expectWord();
	location.redirect_target = expectWord();
	expect(TOKEN_SEMICOLON);
	location.redirect_code = std::atoi(code.c_str());
}

void ConfigParser::parseCgi(LocationConfig& location) {
	std::string extension = expectWord();
	std::string interpreter = expectWord();
	expect(TOKEN_SEMICOLON);
	location.cgi_extensions[extension] = interpreter;
}
