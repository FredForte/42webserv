# General Info Dump

## Telnet


## TCP

`Transmisison Control Protocol` makes sure your data arrives sequentially and error-free. You may have heard of "TCP" before as the better half of "TCP/IP" where "IP" stands for "**Internet Protocol**". IP deals primarily with Internet routing and is not generally responsible for data integrity.

## Datagram

Not so error-free, datagram sockets also use IP for routing, but they dont'use TCP;l they use the "**User Datagram protocol**", or **UDP**. They are connectionless, because you don't have to maintian an open connection as you do with stream sockets. You just build a packet slap an IP header on it with destination information and send it out, no connecton needed.

Sample applications: `tftp` (trivial file transfer protocol, a little brother to FTP), `dhcpdc` (a DHCP client), multiplayer games, streaming audio, video conferencing, etc.

In order to guarantee that the packets are received using UDP, those probram have their own protocol on top of UDP. They wait for an acknowledge packet that says the received got the pack. If not, they send it again. This acknowledgement proceduro is very important when implementing reliable `SOCK_DGRAM` applications.

## Ports

In a Unix box, on `/etc/services` file. 
- HTTP (the web) is port 80 
- HTTPS is 443, telnet
- SMTP is port 25

Ports under 1024 are often considered special, and usually require special OS privileges to use.

# DevLog

## GET/POST/DELETE
They are splint into dedicated handlers `response_handlers.cpp` and hpp

- `handleGetRequest` : file, directory, autoindex, 404, 403 logic
- `handlePostRequest` : Checks `location.upload` for enabled, extracts the basename of the request path as filename ( rejecting `./../` so the write can never espace `upload_store`), writes `request.body` to disk and returns 201/400/403/500.
- `handleDeleteRequest` : Resolves `location.root` + path, 404 if missing, 403 if not a regular file, `unlink()` it and return 204/404/403/500

`getResponseMessage` in `utils_config_file.cpp` is a dispatcher on `request.method` (defaulting to 405 for anything else).

`joinPath` helper to replace the inline slash-checking, preventing it from duplicating the root + path and index + path.

# Tester
On the `tests` directory we have a script `run_tests.sh` and a set of test requests that we can run and tests the webserv behaviour, its interactive and allow us to select specific tests as we go. 

It works like this:
- Boots `webserv` using a config provided `./run_tests.sh [config-path]`, if none is provided it uses `config/upload_test.conf`, it will also build if the `webserv` binary is not found.
- Lists every `*.http` file in `tests/`, prompts you to pick one or `a` for all or `q` to quit.
- For each pick: prints the raw request, sends it over a real TCP socket via `nc -q 1` with a 2s reply cap since `webserv` is not closing the connection after responding yet, then prints the raw response.
- Kills the server on exit/quit/Ctrl+C via a trap, so nothing lingers

# Redirection
A redirect isnt a page, its a status code (301, 302, 307, 308, etc) plus a `Location:` header teling the client "the thinkg you asked for is actually over there". The client (browser, curl with `-L`) reads that header and automatically re-requests the new URL. No file gets read, no body matters much.

The main mechanic is which code to use:
- `301` Moved Permanently / `308` : this is permanent and update your links
- `302` Found / `307` : temporary, keep asking this URL again next time
- `307/308` additionally guarantee the method and body are preserved on the follow-up request. 301/302 traditionally downgrade POST - GET.

In our webserv structure we have it on `HttpResponse` using the `redirect_location` field, where `parseResponseToOutPut` emits a `Location:` header only when its not empty.
`buildRedirectResponse` that is on our `response_handlers.cpp` builds the 3xx response from `location.redirect_code/location.redirect_target`, not per-code special-casing it just emits whatever the config says, that is enough for our webserv project
Redirection is check on `main.cpp` where `responseLocation->redirect_code != 0` before the methods-allowed check, so a `return 301 /new;` location fires regardless of method, which follows the redirect setup on `.conf` files where not `method` is provided.

# Chunked

Our chuncked-decoding logic is in `HttpRequestParser` and is spec-correct:

- Hex size + chunk-extensions: `strtoul(...,16)` parses multi-digit hex and stops at `;`, so `c;ext=ignored\r\n` is handled.
- Embedded `\r\n` inside the chunk data is handled as expected because the chunk-data is is sliced by exact byte count rather than scanning for a line ending, so a payload containing the eof inside its body will come through as content.
- Data split across multiple `recv()` calls, our webserv wait for a full chunk before treating the request.

# Content-Type
We follow a file extension detection path, lowercasing it before the tests to prevent case sensitive edge-cases.
`getContentType(path)` extracts the file extension using `static getFileExtension()` that looks it up in a small media type extension (MIME) table and apends `; charset=UTF-8`for th etext-bases types. Unrecognized extensions fall back to `application/octet-stream`. (that is the usual fallback of server).

# Connection
We are using `determineConnection(const HttpRequest &req)` to decide the connection type to send on our responses, HTTP 1.1 default to `keep-alive`, HTTP 1.0 defaults to `close`, and we are using the clients `Connection` to override the defaults. The strucute on the `buildRedirectReponse` had to receive the HttpRequest as an addition, not that clean, but it is working as expected.

Our main needs to also read the connection flag in order to close or keep the conneciton open for the responded client. 