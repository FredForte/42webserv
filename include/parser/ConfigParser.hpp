#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include "../../include/parser/ConfigTypes.hpp"
#include "../../include/parser/Tokenizer.hpp"

// Recursive-descent parser over the token stream produced by Tokenizer.
// Happy-path only for now: assumes the config is well-formed. Validation
// and error reporting (reject and refuse to start on bad config) come later.
class ConfigParser {
public:
	ConfigParser(const std::string& source);

	Config parse();

private:
	Tokenizer _tokenizer;

	ServerConfig parseServer();
	LocationConfig parseLocation(const std::string& path);

	void parseListen(ServerConfig& server);
	void parseServerName(ServerConfig& server);
	void parseErrorPage(ServerConfig& server);
	void parseClientMaxBodySize(ServerConfig& server);

	void parseMethods(LocationConfig& location);
	void parseRoot(LocationConfig& location);
	void parseIndex(LocationConfig& location);
	void parseAutoindex(LocationConfig& location);
	void parseUploadStore(LocationConfig& location);
	void parseReturn(LocationConfig& location);
	void parseCgi(LocationConfig& location);

	std::string expectWord();  // consumes a TOKEN_WORD, returns its value
	void expect(TokenType type); //todo consumes the next token (type unchecked for now)
};

#endif
