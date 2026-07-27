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

`queue_response` overrides all of the above with `close` when `close_after_response` is set — see [Connection Close Honesty](#connection-close-honesty).

# Cgi Environment
We use the function `std::vector<std::string> buildCgiEnv(request, server, script_path)` at `utils_config_file.cpp` — `script_path` is the alias-resolved on-disk location, see [CGI Env: URL space vs disk space](#cgi-env-url-space-vs-disk-space) for which variables take it and which take the request path.
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
- Body → stdin (POST): **done** — but *not* with the second pipe sketched above.
  The body is staged into a temp file which is `dup2`'d onto the child's stdin,
  because a pipe buffer is 64KB and anything bigger would deadlock the parent
  writing into it. `CONTENT_LENGTH` tells the script how much to read.


# Cgi Response Parsing
We have `HttpResponse paraseCgiResponse(cgi_output, server, request)` at `utils_config_file.cpp`, that does:
-  splits headers/body (CRLF or LF)
- maps `Status` / `Content-Type` / `Location` onto the dedicated fields
- routes everything else ( like :Set-Cookie) into `extra_headers`
- defaults to a bare `Location`to 302, and falls back to whole-body-as-is when a script emits no header block

## Cgi Integration
In our main response from CGI block:

This now lives in `complete_cgi_request` (`main_functions_utils.cpp`) rather than
inline in `main.cpp`, and goes through `queue_response` so the Connection header
and the send offset are set consistently:

```cpp
HttpResponse response = parseCgiResponse(
    client.cgi_instance.cgi_response,       // NOTE: emptied - the buffer is
    *client.ServerConfig_ptr,               // swapped into response.body
    client.request_data);
queue_response(epoll_instance, client, response);
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
We have `reap_timed_out_cgis` in `main_functions_utils.cpp`, it iterated on all saves cgi processes that are running and checking for the time span withtout a response. It also finishes runs whose stdout closed before the child was reapable — see [CGI Lifecycle](#cgi-lifecycle).

And in `main.cpp` on our main loop we run a `epoll_wait` that holds 1 seconds when server is idle, to pool everysecond while we have a cgi running, so we can monitor using `reap_timed_out_cgis`. The 1 seconds in it only holds it when no epoll event is received.

- idle: sleeps up to 1 second
- busy: wakes on every event and processses normally, no server lock.

# Client Max Body Size
We have a DoS guard while the request is still incomplete, if the buffer brows past `client_max_body_size + 16KB` (header size) before a full request arrives, it rejects with 413. This prevents a client from exhausting memory with a huge or never-ending body.

`0` as max body on location .conf means unlimited (same as nginx does).

On `client_connection.hpp, main.cpp` we have `close_after_response`flag, making the EPOLLOUT loop drop the connection once a terminal response (413) is sent.

The directive is honoured inside a `location` block too, overriding the server's — see [Client Max Body Size per Location](#client-max-body-size-per-location).

# Improvements

## Root Setting on .conf
On the `.conf` file, right now we need to have a `root` set for each location that allows for the methog `GET`, it would be better in the future to have a fallback from `location` lever to `server` level settings in case a `GET` does not find a set `root`. 

Have a new header file with the server defaults, so it can be easily tuned-up.
with std max timeout, header_allowance size, buffer_size for our input, ...

# Bugs Solved
We had abrupt disconnects crash the server. In `standard_connecitons_func`, when a `recv()` returned `-1` we would `fail_and_exit_with_message(...)`, which killed the entire process. When a client fires `GET / HTTP/1.1`, then drops the socket right away, `recv()` returns `-1` with `ECONNRESET` (Connection reset by peer).

- Fixed by treating `recv() <= 0` as a situation that would only drop the current client (remove from epoll, erase state, `close()` the fd) and keep the server loop alive. Since we can not read `errono` after `recv()` as stated by the subject, we treat it as a closing case.
- We were also testing for `recv() == 0` removing from the poll but never closing it.

Resused fd routed to the wrong server block. The kernel recicles the fds. `new_connections_func` used `client_fd_to_port.inser(...)`, but `std::map::insert` does not overwrite an existing key. A recycled fd kept its stale port mapping, so a new connection on `:8082` resolved to the previous port on that not recycles fd.

- Fixed by switching to `operator[]` which overwrites.

## Found using Valgrind
We had memory leak from `getaddrinfo()` in `socket_utils.cpp`, where we never `freeaddrinfo()`.

- Fixed by adding the `freeaddrinfo()` and also the missing return-value check.

Uninitializd bytes, where every `epoll_event` passed to `epoll_ctl` had uninitialized padding: the `data` field is an 8-byte union but the code only set `.data.fd` (4 bytes)

- Fixed by zero initialized `memset()` all `epoll_event` declarations we have.

# Request Framing State
`RequestFraming` (`HttpRequestParser.hpp`) lives on `client_connection.framing` and carries what is already known about the request currently filling `input_buffer`, so each `recv()` only inspects the bytes it just added instead of re-reading from byte 0.

- header block located and measured once: `headers_done`, `header_length`
- body framing latched once: `chunked`, or `has_body_length` + `content_length`
- `header_scan` resumes the blank-line search at `size - 3`, so a terminator split across two reads is still caught without re-scanning the front
- `chunk_scan` parks on the first chunk that is not fully buffered, so chunks already received are never re-walked

Cases to cater: the request can be split at **any** byte (mid-header, across the blank line, mid chunk-size line, mid chunk-data). Call `framing.reset()` when a request is consumed and on 413, or the next request on that keep-alive connection gets measured with the previous one's numbers.

`parse()` still exists for one-shot callers; it just runs `parseInto` on a throwaway request.

# Response Draining
`client_connection.output_sent` marks how far into `output_buffer` we have sent. Drain by advancing it, never by `erase(0, n)` from the front: erase has to memmove everything still queued, which turns into quadratic work the moment a send only drains part of the buffer (i.e. as soon as several clients compete for bandwidth). One client hides it completely, since the socket swallows the whole response in a few sends.

Reset `output_sent` to 0 anywhere `output_buffer` is replaced: `queue_response`, and the standard-response block in `main.cpp`.

# CGI Lifecycle
`cgi_instance_struct` is owned by the connection and reused for every request it makes, so "nothing running" has to be representable: `-1` for both `cgi_fd` and `cgi_pid`. Not `0` — that is a real descriptor (stdin), and once a run ends the kernel is free to hand its old fd number straight back to the next `accept()`.

Helpers in `main_functions_utils.cpp`:
- `resetCgiInstance` — back to idle, drops collected output
- `detach_cgi_fd` — unregister from epoll, close the pipe, drop the map entry; optionally kill+reap. Idempotent, so terminal paths call it blindly
- `complete_cgi_request` — finished run into a response (502 on bad exit, else parsed output), then detach + reset
- `drop_client` — full client teardown *including* any CGI still in flight

Cases to cater:
- **every** terminal path resets: success, 502 (non-zero exit), 502 (read error), 504 (timeout), and the `execute_cgi` throw — it can fork before throwing, leaving a pid it already reaped
- **pipe EOF with the child not yet reapable**: take the fd off epoll immediately. A pipe whose writer is gone stays readable forever, so leaving it armed makes epoll hand it back on every wakeup — a full-CPU spin until the child exits or the timeout fires. The reaper finishes it instead
- **client disconnects mid-run**: kill the child. The reaper skips entries whose client is gone, so nothing else would ever collect it

`reap_timed_out_cgis` therefore has two jobs: kill + 504 anything past its `cgi_timeout`, and finish runs parked with `stdout_closed` once they become reapable.

# Connection Close Honesty
Whenever `close_after_response` is set, the response **must** advertise `Connection: close`. `queue_response` rewrites the header in place (copying an `HttpResponse` would duplicate the whole body), so the flag has to be set *before* the response is built — see `reject_with_413`.

Why it matters: a pooling client (Go's `http.Client`, which the 42 tester uses) believes `keep-alive`, returns the socket to its idle pool, sends its next request on it and gets an RST back. It is intermittent by nature — and curl does not reuse connections aggressively enough to ever show it, so a curl-based check cannot catch this.

# Client Max Body Size per Location
`LocationConfig` carries `has_client_max_body_size` + `client_max_body_size`. The separate flag is required because `0` already means "unlimited", so it cannot double as "unset". `resolveMaxBodySize(server, location)` returns the location override when present, otherwise the server value; a NULL location falls back to the server.

Applied at both enforcement points:
- the **early reject**, while the body is still arriving — `peekRequestPath()` pulls the target out of the partial request line so the right location's limit is used before the body has landed
- the **exact check** after parsing, which needs `findRequestedLocation` resolved first

# Body Copy Discipline
A body is buffered once and then handed along by reference or by swap. Every one of these was a full copy of the upload:

- `parseInto(raw, out)` fills the connection's own `request_data` — no `substr` of the buffer, no return-by-value, no assignment into place
- `body.assign(raw, pos, len)` instead of `= raw.substr(...)`, which builds a whole second copy and then copy-assigns it
- `parseResponseToOutPut(const HttpResponse&, std::string& out)` serialises straight into `output_buffer` (by-value + by-return was two more copies)
- `parseCgiResponse(std::string& cgi_output, ...)` **empties its argument**: the buffer is swapped into `response.body` and the header prefix erased in place. Safe only because the caller resets the instance right after — a trap for any future caller
- `request_data.body` is released once `execute_cgi` has staged it to the child's stdin temp file; it is dead weight for the rest of the run
- `input_buffer` is released with `std::string().swap()` above 64KB. `clear()` only resets the length — libstdc++ keeps the allocation, so a connection that once carried a large upload holds that memory for as long as it stays open

# Read Buffer
One shared scratch buffer (`BUFFER_SIZE` in `main.cpp`) for every `recv()` and every CGI pipe read, so its size costs us once rather than once per connection. Sized at 2MB, under `net.core.rmem_max` (4MB here) — past the socket buffer ceiling there is never that much waiting to be read.

No `memset` before a read: `recv`/`read` report exactly how many bytes they wrote and only those are ever used. Zeroing the buffer first would cost more than the read itself at this size.

# CGI Env: URL space vs disk space
Two distinct sets of meta-variables, never to be mixed:
- **URL space** — `PATH_INFO`, `SCRIPT_NAME`, `REQUEST_URI` follow `request.path`
- **disk space** — `PATH_TRANSLATED`, `SCRIPT_FILENAME` follow the alias-resolved on-disk path

The 42 `cgi_tester` rebuilds the request from `REQUEST_URI` and refuses to run unless `PATH_INFO` equals that URL's path, so `PATH_INFO` must never carry the on-disk location. `cgi-bin/pathinfo.py` is the fixture that pins both halves.

# POST Acceptance
A location needs `upload_store` (or a `cgi` entry) to accept POST at all — without it `upload_enabled` is false and the answer is 403 before anything else is looked at.

A POST aimed at the location prefix itself (`POST /post_body`) carries no filename to store under, and a body is optional for POST anyway. That is a valid request, not a client error: 200, nothing written.

# Key-changes

## How CGI-ness is decided
There is no per-connection "this is a CGI connection" flag any more. CGI is a
property of the **request**, not of the connection: a keep-alive client can ask
for a static file, then a script, then a static file again on the same socket, so
a flag set once at connection time would be wrong from the second request on.

Three different questions get asked, and each is answered from live state rather
than from a stored flag:

**1. Should *this request* go to CGI?** Decided fresh per request in
`standard_connections_func`:

```cpp
if (responseLocation && !responseLocation->cgi_extensions.empty()) {
    // ... cgi_extensions.find("." + getFileExtension(request.path))
```

Both have to hold: the location configures CGI **and** the requested extension is
in its map. An extension that is not configured falls through to the normal
response path, where a missing file becomes a 404.

**2. Is this fd a CGI pipe or a client socket?** Map membership, checked by the
event loop on every wakeup:

```cpp
bool is_this_a_cgi_fd(const std::map<int, int>& cgi_fd_map, int this_fd) {
    return cgi_fd_map.find(this_fd) != cgi_fd_map.end();
}
```

**3. Does this connection have a CGI running right now?** The sentinels:
`cgi_fd >= 0` and `cgi_pid > 0`. That is what `detach_cgi_fd` keys off to decide
whether there is anything left to close or kill, and why those fields must be
returned to `-1` on every terminal path — see [CGI Lifecycle](#cgi-lifecycle).

`client_connection_struct::client_connection_type` (with its `STANDARD` / `CGI`
enum) used to carry this. Superseded on all three counts and now **removed**: it
was only ever assigned `STANDARD` and never read, so it reported `STANDARD` even
while a CGI was running. Do not reintroduce it as a check — use the three above.

## Cookies live in the CGI, not the server
`cookie_id` and `cookie_data` were removed from `client_connection_struct`. Two
reasons, and the second is the one that matters: that struct is per **socket**,
and a session has to outlive a socket (a browser opens several connections in
parallel and reconnects constantly). `cookie_id` was also set to the fd number,
which the kernel recycles, so two unrelated clients could have collided on one
session identity.

The server now only forwards — `HTTP_COOKIE` in, `Set-Cookie` out — and
`cgi-bin/session.py` owns the session. See
[features.md](features.md#10b-cookies-and-sessions).