# Webserv Features

What the server guarantees, feature by feature: the rule, the `.conf` that steers
it, and the cases it has to survive. Written to be read when you are about to
change something and want to know what you might break.

- [flow.md](flow.md) — where you are in the code, step by step
- [info.md](info.md) — background notes, devlog, bugs already solved

1. [Configuration](#1-configuration)
2. [Virtual hosts and routing](#2-virtual-hosts-and-routing)
3. [Serving files](#3-serving-files)
4. [Uploads](#4-uploads)
5. [Deletes](#5-deletes)
6. [Redirects](#6-redirects)
7. [Request framing](#7-request-framing)
8. [Body limits](#8-body-limits)
9. [Connection lifetime](#9-connection-lifetime)
10. [CGI](#10-cgi) — [Cookies and sessions](#10b-cookies-and-sessions)
11. [Error responses](#11-error-responses)
12. [Resource discipline](#12-resource-discipline)
13. [Test surfaces](#13-test-surfaces)

---

## 1. Configuration

Parsed and validated before any socket is opened, so a broken config fails at
boot with a message instead of at request time with a 500.

**Rejected at boot:** no `server` block · a server with no `listen` · port outside
1..65535 · two locations sharing a path · a location path not starting with `/` ·
a method other than `GET`/`POST`/`DELETE` · a `return` code outside 3xx · a
location allowing `POST` with neither `upload_store` nor `cgi`.

That last one exists because such a route can only ever answer 403 — better to
say so at boot. `return` locations are exempt: they answer every method with the
redirect and never read a body.

**Cases to cater:** an unknown directive inside a block is skipped, not an error —
the tokenizer walks past it. If you add a directive, add it to the parser
*and* decide whether the validator should care.

---

## 2. Virtual hosts and routing

One listening socket per distinct port; several `server` blocks may share a port.
The `Host` header picks between them, and resolution never fails — an unknown
host falls back to the first server on that port, as nginx does.

Location matching is **longest prefix**, so `/directory/nop` wins over `/` for
`/directory/nop/x`.

`root` is an **alias**, not a document root: the matched prefix is stripped
before joining. `location /static { root www/; }` serves `/static/index.html`
from `www/index.html`, never `www/static/index.html`.

**Cases to cater:** the `Host` header may carry `:port` (matched on the name
alone) or be absent entirely (fall back). A recycled client fd must not keep its
previous port mapping — that is why `client_fd_to_port` is written with
`operator[]` and not `insert`.

---

## 3. Serving files

`GET` resolves the target under the location's root, then:

| target | answer |
|---|---|
| regular file | 200 + body, `Content-Type` from the extension |
| directory with `index` present | 200 + that file |
| directory, no index, `autoindex on` | 200 + generated listing |
| directory, no index, `autoindex off` | 403 |
| missing | 404 |
| exists but not a regular file | 403 |

Content types come from a small extension table, lowercased first so `.JPG` and
`.jpg` agree; text types get `; charset=UTF-8`, anything unknown falls back to
`application/octet-stream`.

---

## 4. Uploads

`POST` needs `upload_store` in the location — without it the location has no
place to put a body and answers 403. (The [validator](#1-configuration) refuses
such a config at boot unless the location has a `cgi` instead.)

The stored filename is the **basename** of the request target, so a path
containing `..` can never write outside `upload_store`.

**Cases to cater:**
- POST at the location prefix itself (`POST /post_body`) carries no filename and
  may carry no body. That is a valid request: **200, nothing stored** — not a 400.
- an empty body with a filename is still an upload; it writes an empty file.

---

## 5. Deletes

`DELETE` resolves like a GET, then `unlink()`: 204 on success, 404 if missing,
403 if the target is not a regular file, 500 if the unlink fails.

---

## 6. Redirects

`return CODE TARGET` in a location emits that status with a `Location:` header
and no meaningful body. It is checked **before** the methods list, so a redirect
location answers every method identically — which is why such a location does not
need `methods` set at all.

Whatever code the config says is emitted as-is; there is no per-code special
casing. Worth knowing when choosing: 301/308 are permanent, 302/307 temporary,
and only 307/308 guarantee the method and body survive the follow-up.

---

## 7. Request framing

A request is dispatched only once fully buffered. Both framings are supported:

- **Content-Length** — exact byte count
- **Transfer-Encoding: chunked** — reassembled into one contiguous body, so
  nothing downstream ever sees chunk framing. Hex sizes and chunk extensions
  (`c;ext=ignored`) are handled, and chunk data is sliced by byte count rather
  than scanned, so a payload containing `\r\n` comes through intact.

Framing progress is latched per connection so each `recv()` only examines the
bytes it just added — see [Request Framing State](info.md#request-framing-state).

**Cases to cater:** the request can split at **any** byte — mid-header, across
the blank line, mid chunk-size line, mid chunk-data. The framing state must be
reset when a request is consumed *and* on a 413, or the next request on that
keep-alive connection is measured with the previous one's numbers.

**Known limit:** pipelining is not supported. Bytes arriving after a complete
request are discarded with the buffer.

---

## 8. Body limits

`client_max_body_size` caps request bodies. `0` means **unlimited**, not "reject
everything". Settable on the server block and overridable per location — the
location wins when it declares one.

Enforced twice:
- **early**, while the body is still arriving: a declared `Content-Length` over
  the limit, or a buffer that grows past `limit + 16KB` of header slack, is
  rejected immediately. This is the DoS guard — it stops a client exhausting
  memory with a huge or never-ending body.
- **exact**, after parsing, against the real body size.

Over-limit answers 413 and closes the connection.

**Cases to cater:** the early check runs before the location is fully resolved,
so the target is pulled from the partial request line to pick the right limit.
Because `0` already means unlimited, a location needs a separate "is it set"
flag internally — see
[Client Max Body Size per Location](info.md#client-max-body-size-per-location).

---

## 9. Connection lifetime

HTTP/1.1 defaults to keep-alive, HTTP/1.0 to close; the client's `Connection`
header overrides either. A kept-alive connection is returned to `EPOLLIN` and
reused for the next request.

**The invariant that matters:** if the server is going to close, the response
must already say `Connection: close`. Answering `keep-alive` and then hanging up
leaves a pooling client (Go's `http.Client`, which the 42 tester uses) reusing a
dead socket and getting an RST instead of a reply. Intermittent by nature, and
curl will not reproduce it —
[Connection Close Honesty](info.md#connection-close-honesty).

**Cases to cater:** every per-connection field is reused across keep-alive
requests. Anything left from the previous request — framing state, CGI slot,
send offset, buffers — has to be explicitly reset.

---

## 10. CGI

Configured per location as `cgi .ext [interpreter]`. With an interpreter the
script is passed as its argv[1]; with none (`cgi .bla ;`) the script is executed
directly as a binary.

**Routing.** Whether a request goes to CGI is decided **per request**, never per
connection — a keep-alive client can ask for a static file, then a script, then a
static file again on one socket. Two things must both hold: the matched location
configures at least one `cgi`, *and* the requested path's extension is in that
location's map. An extension that is not configured falls through to the normal
response path, where a missing file is a 404 — so a `.txt` inside a CGI directory
is a 404, not an attempt to execute it.

There is no "this is a CGI connection" flag; the three live checks that replaced
it are written up in [How CGI-ness is decided](info.md#how-cgi-ness-is-decided).

**Environment.** Full CGI/1.1 meta-variable set plus request headers as
`HTTP_*`. The split that matters: `PATH_INFO`, `SCRIPT_NAME` and `REQUEST_URI`
are **URL space**; `PATH_TRANSLATED` and `SCRIPT_FILENAME` are the
**alias-resolved on-disk path**. The 42 `cgi_tester` rebuilds the request from
`REQUEST_URI` and refuses to run unless `PATH_INFO` matches that URL's path —
[details](info.md#cgi-env-url-space-vs-disk-space).

**Input.** Two separate channels, easily confused:
- `?`-parameters go to the `QUERY_STRING` env var, **never** stdin — this is how
  GET passes parameters.
- the request body goes to **stdin**, and the script reads exactly
  `CONTENT_LENGTH` bytes — this is how POST passes data.

The body is staged into a temp file rather than a pipe, because a pipe buffer is
64KB and anything larger would deadlock. The child also `chdir`s into the
script's directory so relative file access works.

**Output.** The script's own header block is split off (CRLF or bare LF) and
mapped onto the response: `Status`, `Content-Type`, `Location`, everything else
(e.g. `Set-Cookie`) passed through. A bare `Location` defaults to 302. A script
that emits no header block at all has its whole output treated as the body.

**Lifetime.** `cgi_timeout` seconds per location (default
`CGI_TIMEOUT_DEFAULT_SECONDS`), after which the child is SIGKILLed and the client
gets 504. The event loop polls at 1s while any CGI is registered so the reaper
runs even when nothing else happens.

**Cases to cater:**
- the connection reuses one CGI slot for every request it makes, so **every**
  terminal path must reset it: success, 502 (bad exit), 502 (read error), 504
  (timeout), and a throw out of `execute_cgi` — which can fork before throwing.
- the pipe can reach EOF while the child is not yet reapable. It must come off
  epoll at that point: a pipe with no writer stays readable forever, so leaving
  it armed spins the loop at full CPU.
- a client that disconnects mid-run must take its child with it, or nothing
  collects it — the reaper skips entries whose client is gone.

Full state machine: [CGI Lifecycle](info.md#cgi-lifecycle).

---

## 10b. Cookies and sessions

The server owns **none** of this. It forwards in both directions and that is all:

- the client's `Cookie` header arrives at the script as `HTTP_COOKIE`, like any
  other header
- whatever `Set-Cookie` the script emits is passed back to the client through the
  response's extra headers

The session itself lives in `cgi-bin/session.py`: no cookie means a fresh random
token is minted and `Set-Cookie`'d, a returning cookie is recognised and the
visit count advances. The count is kept in a file under `/tmp/webserv_sessions`
because each CGI run is a new process and nothing survives in memory.

Deliberately kept out of the server core. Session state cannot live on
`client_connection_struct` — that is per **socket**, and a session has to outlive
a socket: a browser opens several connections in parallel and reconnects
constantly. (An earlier `cookie_id` field was keyed to the fd number, which the
kernel recycles, so two unrelated clients could have collided.)

**Cases to cater:**
- the cookie value comes from the client and becomes part of a filename in the
  session store, so anything the script did not mint must be refused — otherwise
  `Cookie: session_id=../../etc/passwd` walks out of the store. The script
  accepts only its own 32-hex format.
- `Set-Cookie` is emitted only on the first visit; afterwards the client already
  holds it.

**Known limit:** `extra_headers` is a map keyed by header name, so a CGI emitting
**two** `Set-Cookie` headers loses the first. One cookie per response works
fine; if that ever needs to change, the field has to become a multimap.

---

## 11. Error responses

| where | how |
|---|---|
| boot | `fail_and_exit_with_message` — socket setup, `listen`, `epoll_create1`, initial `epoll_ctl` |
| building a response | any throw becomes a 500 for that request only |
| CGI setup failure | 500 |
| CGI script failure (non-zero exit or killed) | 502 |
| CGI over `cgi_timeout` | 504 |
| body over the limit | 413, connection closed |
| method not in `methods` | 405 |
| no location matched / target missing | 404 |

`error_page CODE path` in the server block supplies a custom body; otherwise a
built-in default page is used.

A per-client failure never takes the server down — it drops that one connection
and the loop continues.

**Known limit:** the subject forbids inspecting `errno` after `recv`/`send`, so a
genuinely dead socket cannot be told apart from a transient condition. Both are
treated as "drop this client".

---

## 12. Resource discipline

The server must stay flat under load, so:

- **fds** — every CGI pipe is closed and unregistered on every terminal path;
  a disconnecting client takes its pipe with it.
- **processes** — every child is reaped; none outlives its connection.
- **memory** — a request body is buffered once and then passed by reference or
  swapped, never copied. Large per-connection buffers are released rather than
  `clear()`ed, because `clear()` keeps the allocation and a keep-alive connection
  would hold it for as long as it lives.
  See [Body Copy Discipline](info.md#body-copy-discipline).
- **CPU** — responses drain by advancing an offset, never by erasing from the
  front of the buffer, which is quadratic once a send only partially drains.
  See [Response Draining](info.md#response-draining).

Reference points after a full 42 tester run: RSS back to single-digit MB, fd
count back to baseline, zero zombies, no leftover `/tmp/webserv_cgi_stdin_*`.

---

## 13. Test surfaces

| tool | what it is for |
|---|---|
| `tests/e2e.sh` | the regression gate. Non-interactive, asserts on real responses, exits non-zero on any failure. Run it after every change; `-v` dumps requests and responses. |
| `tests/tester` | the 42 tester. Must exit 0 with no `FATAL` line. Includes the concurrency and 100MB-body stress tests. |
| `tests/run_tests.sh` | interactive; fires the raw `.http` files in `tests/` one at a time and prints the raw response. |
| `tests/valgrind.sh` | leak checking. |

Fixtures worth knowing: `cgi-bin/slow.py` never exits (proves the timeout),
`cgi-bin/fail.py` exits non-zero (502), `cgi-bin/closes_stdout.py` closes stdout
before exiting (the EOF-before-reap case), `cgi-bin/pathinfo.py` pins the
URL-space vs on-disk env split.

**Cases the suite deliberately covers, because they were once broken:** requests
split at 1 byte per write, chunked and Content-Length; two requests down one
socket; a client disconnecting mid-CGI; a config that allows POST with nowhere
to store it.
