#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP

#include <string>

enum TokenType { TOKEN_WORD, TOKEN_LBRACE, TOKEN_RBRACE, TOKEN_SEMICOLON, TOKEN_END };

struct Token {
	TokenType type;
	std::string value; // only meaningful when type == TOKEN_WORD
};

// Turns raw config file text into a stream of Tokens. Delimiters { } ; are
// always their own token; anything else contiguous and non-blank is a WORD.
// '#' starts a line comment.
class Tokenizer {
public:
	explicit Tokenizer(const std::string& source);

	Token next(); // consumes and returns the next token
	Token peek(); // returns the next token without consuming it

private:
	std::string _source;
	size_t _pos;
	bool _has_peeked;
	Token _peeked;

	void skipWhitespaceAndComments();
	Token readToken();
};

#endif
