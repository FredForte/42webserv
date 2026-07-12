# Full Flow of the Tokenizer and Parser
At first i am using the nginx.conf reference as we talked about.
This system for now is following the happy path, you can see the `expect()` funciton is not checking if it got the actual expected token yet. 

The .conf file can have `#` for comments, where we just skip to the `\n` when its detected.

The whole file is treated as a single string, this way we will use our key characters to know what we are getting from it: `{ } ;`. 

## Tokenizer
The Tokenizer is detecting it and saving the substrings according to the enum we have on the Tokenizer header:
`enum TokenType { TOKEN_WORD, TOKEN_LBRACE, TOKEN_RBRACE, TOKEN_SEMICOLON, TOKEN_END };`

We also have the main fuctions that we will use for this class: `next()` and `peek()`. 
Peek is not necessary but keeps the use cleaner, this way we can see forward withtout trully consuming the token list.

For that we have a flag `_has_peeked` and the actual token that has been peeked `_peeked`. If we request peek or next we check if we have something peeked and return it instead of consuming the token list. 

## Parser
For our parser i have defined an initial structure that we can change if something else shows up on our tests.

I have commented the reason why i have those variables and this strucute on `../../include/parser/ConfigTypes.hpp`.

To use our parser, focus on consuming the `Config` vector, that lists our `ServerConfig`. It's the return of the funciton `parse()` that we run after having the `ConfigParser` object initialized with a source of type `std::string &source`. 

The use flow: read config file, make a string from the `stringstream` then make a `ConfigParser` object sending this string on it's constructor, then run `parse()` on this object saving the return as a `Config` vector, there is where we should consume the parsed information to execute the requesets by.

---

**I've made a separate folder as well as a separate main file just to test how we are consuming the configuration file.** 

Run `make parser_test`

---

## Http Request
For the request parsing I am also reading it as a single string but not tokenizing it, I look for the `\r\n` for each parameter line and set the first line to a different parsing method, the `parseRequestLine`. 

On `parseRequestLine`, i split the first two spaces into `method`, `target` and `version`, then split the `target` further down on the `?` for `path` and `query_string`, and every line after that until an empty line goes to `parseHeaderLine`.

On `parseHeaderLine` I split on the first `:`, trim the whitespacess off the value, lowercase the key before saving on the structure, since the header parameters are case-insensitive, this will keep a pattern on the save data.

I ended up with the structure on `../../include/parser/HttpRequest.hpp`, but if that is not enough we can add or change it. Since I've run a toLowercase function on the header parameters, we can query the std::map using the name wihtout problem.

Maybe I would still need to further parse the `query_string`, but we are set for the initial tests for now.

## Http Request Body
Here I use `content-length` and `chunked transfer`. The `pos` which is the character testing position, advances past the current line before the empty-line check, so when the header end line is detected we get the begining of the body.

`parseBody()` checks the type of transfer, first for `transfer-encoding: chunked` then for `content-length` and if none are present it does nothing, because it means no body was provided.

`parseChunkedBody()` uses `strtoul()` naturally stopping at the first non-hex character, this skipps chunk extensions, maybe that would be a problem, but we need to test.

**This http parser expects a full and well-terminated request, I am not treating for requests that never contain a blank line or defines content-length / chunk size, We should place this checks before getting into the parse method**

## Http Request - Chunked completion
On every `EPOOLIN` wakeup for a socket that is sending chunked:
- `recv()` whatever bytes are available, `append()` them to that connecton buffer
- Use `parser.completeRequestLength(buffer)`
	- If it returns `std::string::npos` means the request is not done yet, so go back to wait the `EPOOLIN` again
	- If it return a `number`, means that the request is in the first `number` of bytes
		- Slice the buffer (`buffer.substr(0, number)`), then run `parser.parse` to retceive the `HttpRequest`, then erase the `number` of bytes from the buffer.

```c
size_t length = parser.completeRequestLength(buffer);
if (length == std::string::npos) {
    // not complete yet - keep recv()-ing into buffer, try again next EPOLLIN
} else {
    // buffer[0, length) is one full request
    HttpRequest request = parser.parse(buffer.substr(0, length));
    buffer.erase(0, length);
}
```


## CPP Tools
I am listing some functions and tools that are new to me and proved to be useful on this flow.
- `std::string::npos` - A variable that is declared and fully initialized inside the `<string>`library own header, it already exists as a compile-time constant to `std::string`. Its a `static const` variable that I am using just as a comparison point to exclude scenarios.
- `strtoul` - String to unsigned long, `<cstdlib>`. 
	`unsigned long strtoul(const char *str, char **endpt, int base);`
		- `str` the C-string to parse
		- `endptr` optional out-parameter, if not null it gets set to point at the first character after the parsed number.
		- `base` what numeral system to interpret, 10 decimal, 16 hexa...
	It skips leading whitespace, reads as many valid digits for that base as it can, and stops at the first character that doesn't fit.
