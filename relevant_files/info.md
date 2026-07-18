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

## End to End Tester
We have a script `e2e.sh` that runs a batch of requests and checks the response based on the `example.conf` file and the expected behaviours of our `webserv`, this server as a good `regression` test that we can run periodically to see if nothing broke after changes. 

The script runs default silent, run with `e2e.sh -v` runs as verbose to output the requests and responses as well.

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

# Cgi Environment
We use the function `std::vector<std::string> buildCgiEnv(request, server)` at `utils_config_file.cpp`
It works with full CGI/1.1 meta-variable set + `HTTP_*` headers, same `KEY=VALUE` vector shape `excecute_cgi` already uses for argv.

## Cgi Env Integration
- Get the env in and onto `execve`, call `buildCgiEnv` in dispatch (where the request lives) and stash it on the instance, the same way `cgi_command.args` works. 
- Add a `std::vector<std::string> env;` to `cgi_instance_struct`
- then in the child of `cgi.cpp` convert and pass it:
	```cpp
	std::vector<char*> envp;
	for (size_t i = 0; i < cgi_instance.env.size(); i++)
		envp.push_back(const_cast<char*>(cgi_instance.env[i].c_str()));
	envp.push_back(NULL);
	execve(bin_path, const_cast<char* const*>(&argv_vector[0]), &envp[0]); 
	```
- Feed the POST body to the scrdipts stdin. When running POST on cgi it nees a second pipe: parent writes `request.body` into it, child `dup2` it's read end onto `STDIN_FILENO`.
- `CONTENT_LENGHT` which `buildCgiEnv` sets, tell the script how may bytes to read.

## Cgi Input: Query String vs Body/stdin
A CGI script receives request data over **two separate channels**, and which one
carries "the parameters" depends on the method. They must not be confused:

- **Query string** (everything after `?` in the URL) → the `QUERY_STRING`
  **environment variable**, never stdin. This is how a **GET** passes params.
  `buildCgiEnv` already sets it from `request.query_string` (the parser splits
  the raw target on `?` into `request.path` + `request.query_string`). A script
  reads it from the environment (e.g. `os.environ["QUERY_STRING"]` in Python).
- **Request body** → the script's **stdin**. This is how a **POST** passes data
  (e.g. an HTML form's `application/x-www-form-urlencoded` fields, which look
  like a query string but live in the body). The script reads exactly
  `CONTENT_LENGTH` bytes from stdin.

So a `?`-param is *always* env (`QUERY_STRING`); it does not travel through
stdin. Only the POST body goes to stdin. A script that wants form params on
stdin is reading the **body** of a POST, which is a different thing from the
URL's `?` section even though both encode `key=value&key=value`.

Status of each channel on our side:
- `QUERY_STRING` (the `?` path): **done** — set by `buildCgiEnv`.
- Body → stdin (POST): **pending** — needs the second pipe in `execute_cgi`
  described above (Julio's dispatch/exec side); `CONTENT_LENGTH` is already set.


# Cgi Response Parsing
We have `HttpResponse paraseCgiResponse(cgi_output, server, request)` at `utils_config_file.cpp`, that does:
-  splits headers/body (CRLF or LF)
- maps `Status` / `Content-Type` / `Location` onto the dedicated fields
- routes everything else ( like :Set-Cookie) into `extra_headers`
- defaults to a bare `Location`to 302, and falls back to whole-body-as-is when a script emits no header block

## Cgi Integration
In our main response from CGI block:

```cpp
HttpResponse cgi_response = parseCgiResponse(
    client_connection->cgi_instance.cgi_response,
    *client_connection->ServerConfig_ptr,   // parseCgiResponse takes a reference
    client_connection->request_data);
client_connection->output_buffer.append(parseResponseToOutPut(cgi_response));

```

# Cgi Timeout
We read per location the set: `cgi_timeout <seconds>` on the .conf file that gets parsed and saved under our `LocationConfig::cgi_timeout` in `size_t` `seconds`, we also have a macro in `ConfigTypes.hpp` for the default timeout value if none is provided `CGI_TIMEOUT_DEFAULT_SECONDS`.

`cgi_timeout <seconds>` is parsed via `parseCgiTimeout` in `ConfigParser.cpp`, that is called from within `parseLocation`.

# Errors Handling
We handle errors differently for each step of the execution and webserver states.

# Server Boot Errors
`failt_and_exit_with_message` handles the errors on webserv boot sequence when running:
- `return_a_fully_prepared_socket`
- `listen`
- `epoll_create1`
- the initial `epool_ctl ADD`

All of this happen once at boot, before any client exits

## Internal Server Error 5xx
We are using throw and catch in order to detect and respond to errors and failures that might occur during the exectuion of certain functions in the server.
calling `void queue_error_response(int epoll_instance, client_connection_struct& client, int status_code)` that prepares a response object with the provided status code and flags it for `EPOLLOUT`.

## CGI error
`execute_cgi` parent side uses `throw std::runtime_error` with fd cleanup before each throw:
- `pipe()` fail goes to throw
- `for()` fail closes both pipe ends, then throw
- parent `epoll_ctl` fail close read end, then throw
- child `execve` fail, `_exit(1)` not `std::exit` and never thow because:
	- `std::exit` would flush stdio buffers that are inherited from parent, double-writing buffered output by leaving it available for parent to read or append it, `_exit()` goes straigh to kernel, so no flush, no destructors. The child's copy is sdiscarded, leaving only the parent created buffer bytes.
	- no throw because that would unwind into the parent's main request loop inside the child process. Making it another webserv instance. So it would basically skips the the child's designated are of action and make it another webserv.

Code:
- 500 on cgi setup failure
- 502 on script failure

## Standard Requests Errors
500s from the POST/DELETE handlers
`try/catch` around the whole build block, so any ynexpected throw becomes `queue_error_response(...,500)` for that request.

## Clients Erros
So per client errors we drop the conneciton, if `send() == -1`, also if no `client_instance` is found we run:
`epoll_ctl(DEL)` -> `close` -> `erase` -> `continue`.

Since we can not use `errno` we cant tell a genuinely dead socket from a `EAGAIN`. So we just close the connection.
If we happen to send a big buffer to a slow client, we would end up closing the connecition from not knowing the correct state, but its a `webserv` project limitation from the start.

# CGI Timout
We have `reap_timed_out_cgis` in `main_functions_utils.cpp`, it iterated on all saves cgi processes that are running and checking for the time span withtout a response.

And in `main.cpp` on our main loop we run a `epoll_wait` that holds 1 seconds when server is idle, to pool everysecond while we have a cgi running, so we can monitor using `reap_timed_out_cgis`. The 1 seconds in it only holds it when no epoll event is received.

- idle: sleeps up to 1 second
- busy: wakes on every event and processses normally, no server lock.

# Improvements

## Root Setting on .conf
On the `.conf` file, right now we need to have a `root` set for each location that allows for the methog `GET`, it would be better in the future to have a fallback from `location` lever to `server` level settings in case a `GET` does not find a set `root`. 