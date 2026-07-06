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

I have commented the reason why i have those variables and this strucute on `ConfigTypes.hpp`.

To use our parser, focus on consuming the `Config` vector, that lists our `ServerConfig`. It's the return of the funciton `parse()` that we run after having the `ConfigParser` object initialized with a source of type `std::string &source`. 

The use flow: read config file, make a string from the `stringstream` then make a `ConfigParser` object sending this string on it's constructor, then run `parse()` on this object saving the return as a `Config` vector, there is where we should consume the parsed information to execute the requesets by.

---

**I've made a separate folder as well as a separate main file just to test how we are consuming the configuration file.** 

Run `make parser_test`