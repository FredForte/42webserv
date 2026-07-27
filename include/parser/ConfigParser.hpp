#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include "../../include/parser/ConfigTypes.hpp"
#include "../../include/parser/Tokenizer.hpp"

// Recursive-descent parser over the token stream produced by Tokenizer.
// Happy-path only for now: assumes the config is well-formed. 
// Validation and error reporting handled on ConfigValidator class.
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
    void parseCgiTimeout(LocationConfig& location);
    void parseClientMaxBodySize(LocationConfig& location);

    std::string expectWord();    // consumes a TOKEN_WORD, returns its value
    void expect(TokenType type);
};

#endif
