# Webserv Flow

How a byte gets from a socket to a response, and which `.conf` rules steer it.
Companion to `info.md` that explains *why* each mechanism exists, and this
explains *where* you are in the code.

1. [Boot](#1-boot)
2. [The event loop](#2-the-event-loop)
3. [Reading a request](#3-reading-a-request) — [Framing](#31-framing) · [Parse and dispatch](#32-parse-and-dispatch)
4. [Building a standard response](#4-building-a-standard-response) — [POST](#41-post)
5. [CGI request](#5-cgi-request) — [Launch](#51-launch) · [Collect](#52-collect) · [Retire](#53-retire)
6. [Writing the response](#6-writing-the-response)
7. [What the conf says](#7-what-the-conf-says) — [server](#server-block) · [location](#location-block) · [boot rules](#rules-enforced-at-boot)
8. [Where a connection can end](#8-where-a-connection-can-end)

---

## 1. Boot

`main.cpp`

```
readFile(conf) -> ConfigParser::parse() -> ConfigValidator::validate()
                -> one listening fd per distinct port
                -> epoll_create1
```

The config is parsed and validated **before** a single socket is opened. A bad
config aborts startup with a message rather than producing a server that can only
answer errors — see [rules enforced at boot](#rules-enforced-at-boot) for what
"bad" means.

Two maps built at boot drive everything after:

| map | meaning |
|---|---|
| `listening_fd_to_port` | which port a listening fd accepts on |
| `port_to_server_config_ptr_mmap` | port -> every `server` block bound to it (multimap: that is what makes virtual hosts work) |

---

## 2. The event loop

`main.cpp`, `while (!g_stop)`

```
epoll_wait(timeout = cgi_fd_map.empty() ? -1 : 1000)
  |
  +-- fd is a listening fd  -> new_connections_func()   accept, non-block, EPOLLIN
  +-- fd is a cgi pipe      -> CGI read branch
  +-- EPOLLIN on a client   -> standard_connections_func()
  +-- EPOLLOUT on a client  -> response drain
  |
  +-- after the batch: reap_timed_out_cgis()
```

Where each branch goes: [cgi pipe](#52-collect) ·
[EPOLLIN](#3-reading-a-request) · [EPOLLOUT](#6-writing-the-response) ·
[reaper](#53-retire).

The 1-second timeout only applies while a CGI is registered — it is what lets the
reaper run even when no event arrives. Idle with no CGI, we block indefinitely.

Per-connection state lives in `client_map` (`fd -> client_connection_struct`) and
survives across keep-alive requests. That reuse is the source of most subtle bugs:
anything left over from the previous request has to be explicitly reset.

---

## 3. Reading a request

`main_functions.cpp :: standard_connections_func`

```
recv() into the shared 2MB buffer      (no memset)
  |
  +-- <= 0 -> drop_client()  (kills any in-flight CGI too)
  |
append to client.input_buffer
  |
peekRequestPath()  -> match location off the partial request line
resolveMaxBodySize(server, location)   -> the limit that applies right now
  |
completeRequestLength(input_buffer, client.framing)   incremental
  |
  +-- npos (still incomplete)
  |     +-- declared Content-Length > max_body      -> 413, close
  |     +-- buffered > max_body + 16KB header slack -> 413, close
  |     +-- otherwise return, wait for more bytes
  |
  +-- got a full request -> parse and dispatch
```

Why no memset: [Read Buffer](info.md#read-buffer). Why the limit is resolved
twice: [Client Max Body Size per Location](info.md#client-max-body-size-per-location).

### 3.1 Framing

`completeRequestLength` keeps its progress in `client.framing` so each call only
looks at the newly arrived bytes. Header block measured once; after that a
Content-Length request is a size comparison and a chunked one resumes its walk
from `chunk_scan`. `framing.reset()` when the request is consumed, and on 413.

Full rules and the cases it has to survive:
[Request Framing State](info.md#request-framing-state).

### 3.2 Parse and dispatch

```
parseInto(input_buffer, client.request_data)   no copy of the body
release input_buffer (swap above 64KB)
framing.reset()
  |
get_server_config_instance_based_on_port_and_hostname()   Host header -> server block
findRequestedLocation()                                   longest prefix match
resolveMaxBodySize() again, now authoritative
  |
  +-- body > max_body -> 413, close
  |
  +-- location has cgi_extensions AND the path's extension is one of them
  |      -> CGI branch
  |
  +-- otherwise -> arm EPOLLOUT, response built in the write handler
```

Branches: [CGI](#5-cgi-request) ·
[standard response](#4-building-a-standard-response). Why `parseInto` instead of
a returned request: [Body Copy Discipline](info.md#body-copy-discipline).

Host resolution never fails: an unknown `Host` falls back to the first server
block on that port, the way nginx does.

---

## 4. Building a standard response

`main.cpp`, EPOLLOUT branch, when `ready_to_respond == false`

```
findRequestedLocation()
  |
  +-- no location matched            -> 404
  +-- location.redirect_code != 0    -> buildRedirectResponse()  answers every method
  +-- method not in location.methods -> 405
  +-- otherwise -> getResponseMessage() dispatches on the method:
        GET    -> handleGetRequest      file / index / autoindex / 404 / 403
        POST   -> handlePostRequest
        DELETE -> handleDeleteRequest   unlink -> 204 / 404 / 403
```

POST is detailed in [4.1](#41-post).

`return` is checked **before** the methods list, because a redirect location
answers everything the same way.

### 4.1 POST

```
location.upload_enabled == false        -> 403     (no upload_store configured)
filename derivable from the target?
  no  -> 200, nothing stored            (POST at the location prefix itself;
                                         a body is optional for POST)
  yes -> write body to upload_store/<basename>  -> 201 / 500
```

Only the basename of the target is used, so a path containing `..` can never
write outside `upload_store`. Rationale:
[POST Acceptance](info.md#post-acceptance). A config that allows POST without a
store or a cgi never reaches here — it is
[rejected at boot](#rules-enforced-at-boot).

---

## 5. CGI request

### 5.1 Launch

`main_functions.cpp` then `cgi.cpp :: execute_cgi`

```
cgi_response.clear()                    previous run's output must not survive
resolve script path against the location's root (alias-style: prefix stripped)
buildCgiEnv(request, server, script_path)
  |
body -> temp file (NOT a pipe: a pipe buffer is 64KB and would deadlock)
pipe() for the child's stdout
fork()
  child : dup2 stdin<-tempfile, stdout->pipe, chdir to the script's dir, execve
  parent: close write end, make read end non-blocking, register EPOLLIN,
          record cgi_fd / cgi_pid / start_time / timeout_seconds,
          cgi_fd_map[cgi_fd] = client_fd
  |
release request_data.body               already staged on disk
return without arming EPOLLOUT          the child still has to produce output
```

`chdir` into the script's directory is what lets a CGI open files by relative
path. `INTERPRETED_LANGUAGE` execs the configured interpreter with the script as
argv[1]; `BINARY` (a `cgi .ext ;` with no interpreter) execs the script itself.

Which env vars carry the URL and which carry the on-disk path:
[CGI Env](info.md#cgi-env-url-space-vs-disk-space).

### 5.2 Collect

`main.cpp`, CGI pipe branch

```
read(cgi_fd)
  |
  +-- -1  -> kill+reap, 502
  +-- >0  -> append to cgi_response, wait for more
  +-- 0   -> EOF: the child closed stdout
        stdout_closed = true
        waitpid(WNOHANG)
          +-- reaped  -> complete_cgi_request()
          +-- not yet -> take the fd OFF epoll and let the reaper finish it
                         (a pipe with no writer stays readable forever; leaving
                          it armed spins the loop at full CPU)
```

`complete_cgi_request` -> non-zero exit or killed by a signal gives 502,
otherwise `parseCgiResponse` splits the CGI's own header block from its body
(swapping the buffer over rather than copying it) and the response is
[queued](#6-writing-the-response). Then `detach_cgi_fd` + `resetCgiInstance` put
the slot back to idle.

The full state machine and every path that must reset it:
[CGI Lifecycle](info.md#cgi-lifecycle).

### 5.3 Retire

`reap_timed_out_cgis`, every loop pass

```
for each registered cgi:
  past cgi_timeout            -> SIGKILL, reap, 504, detach, reset
  stdout_closed and reapable  -> complete_cgi_request()
  client already gone         -> skipped (drop_client already killed it)
```

---

## 6. Writing the response

`main.cpp`, EPOLLOUT branch

```
send(output_buffer.data() + output_sent, size - output_sent)
  |
  +-- -1 -> drop_client()
  +-- advance output_sent          never erase from the front (quadratic)
  |
fully sent?
  +-- close_after_response -> drop_client()
  +-- otherwise            -> back to EPOLLIN, ready_to_respond = false,
                              connection stays open for the next request
```

Why an offset and not `erase`: [Response Draining](info.md#response-draining).

If the connection is going to be closed, the response must already say
`Connection: close` — `queue_response` enforces that, so a pooling client never
reuses a socket we are about to drop. See
[Connection Close Honesty](info.md#connection-close-honesty) and
[where a connection can end](#8-where-a-connection-can-end).

---

## 7. What the conf says

### server block

| directive | effect |
|---|---|
| `listen [host:]port` | a listening socket; repeated for multiple ports |
| `server_name` | matched against the `Host` header among servers on that port |
| `error_page CODE path` | custom body for that status |
| `client_max_body_size N` | default limit for the block; `0` = unlimited |
| `location PREFIX { }` | a route |

Several `server` blocks may share a port — the `Host` header picks between them,
first-on-the-port wins when nothing matches
(see [3.2](#32-parse-and-dispatch)).

### location block

| directive | effect |
|---|---|
| `methods ...` | allowed methods; anything else gets 405 |
| `root DIR` | the prefix is **stripped** then joined onto this (alias-style) |
| `index FILE` | served when the target resolves to a directory |
| `autoindex on\|off` | directory listing when there is no index |
| `upload_store DIR` | enables POST uploads and says where they land ([4.1](#41-post)) |
| `return CODE TARGET` | redirect; answers every method, checked before `methods` |
| `cgi .ext [interpreter]` | run that extension through CGI ([5](#5-cgi-request)); empty interpreter = the script is the binary |
| `cgi_timeout N` | seconds before a run is killed and answered 504 ([5.3](#53-retire)) |
| `client_max_body_size N` | overrides the server's for this location |

Matching is **longest prefix**: `/directory/nop` beats `/` for `/directory/nop/x`.

### Rules enforced at boot

`ConfigValidator`

- at least one `server` block
- every server has a `listen`, with port in 1..65535
- no two locations in a server share a path
- a location path starts with `/`
- methods are only `GET`, `POST`, `DELETE`
- `return` codes are 3xx
- **a location allowing `POST` has `upload_store` or at least one `cgi`** — without
  one there is nowhere for a body to go and the route could only ever answer 403.
  `return` locations are exempt: they answer every method with the redirect and
  never read a body.

### Two easy config mistakes

- `root` is an **alias**: `location /static { root www/; }` serves
  `/static/index.html` from `www/index.html`, not `www/static/index.html`.
- `client_max_body_size 0` means unlimited, not "reject everything". That is why
  a location needs the separate `has_client_max_body_size` flag internally, to
  tell "set to 0" apart from "not set" —
  [details](info.md#client-max-body-size-per-location).

---

## 8. Where a connection can end

| trigger | path |
|---|---|
| `recv() <= 0` | `drop_client` — kills any in-flight CGI |
| `send() == -1` | `drop_client` |
| `Connection: close` requested | drained, then `drop_client` |
| HTTP/1.0 with no keep-alive | same |
| 413 rejection | response says `close`, then `drop_client` |
| SIGINT / SIGTERM | loop exits, everything closed in the cleanup block |

Every one of these goes through `drop_client`, which is the only place that
guarantees a CGI child does not outlive the connection that asked for it
(see [CGI Lifecycle](info.md#cgi-lifecycle)).
