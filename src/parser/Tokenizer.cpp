#include "Tokenizer.hpp"

#include <cctype>

Tokenizer::Tokenizer(const std::string& source) : _source(source), _pos(0), _has_peeked(false) {}

void Tokenizer::skipWhitespaceAndComments() {
    while (_pos < _source.size()) {
        if (std::isspace(static_cast<unsigned char>(_source[_pos]))) {
            _pos++;
        } else if (_source[_pos] == '#') {
            while (_pos < _source.size() && _source[_pos] != '\n')
                _pos++;
        } else {
            break;
        }
    }
}

Token Tokenizer::readToken() {
    skipWhitespaceAndComments();

    if (_pos >= _source.size()) {
        Token t = { TOKEN_END, "" }; //todo: change the value to NULL on these cases if possible
        return t;
    }

    char c = _source[_pos];

    if (c == '{') {
        _pos++;
        Token t = { TOKEN_LBRACE, "{" };
        return t;
    }
    if (c == '}') {
        _pos++;
        Token t = { TOKEN_RBRACE, "}" };
        return t;
    }
    if (c == ';') {
        _pos++;
        Token t = { TOKEN_SEMICOLON, ";" };
        return t;
    }

    // casting here because ::isspace requires a truncated char (0–255, or EOF).
    // static_cast<unsigned char>: truncates to 8 bits → 255. 
    size_t start = _pos;
    while (_pos < _source.size() && !std::isspace(static_cast<unsigned char>(_source[_pos])) &&
           _source[_pos] != '{' && _source[_pos] != '}' && _source[_pos] != ';') {
        _pos++;
    }
    Token t = { TOKEN_WORD, _source.substr(start, _pos - start) };
    return t;
}

// Main functions on class:
// Peek is more of a good feature to keep the config parsers iteration more direct
// next() consumes the token list and peek keeps the 'next' token available for consume 
// when necessary.
Token Tokenizer::next() {
    if (_has_peeked) {
        _has_peeked = false;
        return _peeked;
    }
    return readToken();
}

Token Tokenizer::peek() {
    if (!_has_peeked) {
        _peeked = readToken();
        _has_peeked = true;
    }
    return _peeked;
}
