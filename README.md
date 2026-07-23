***This project has been created as part of the 42 curriculum by fforte-j, juhenriq***

# Webserv - 42 School Project

This project requires students to study HTTP protocol in order to develop a web server using C++ and C. Following a set of requirements that tests our understanding of the protocol, by applying and handling the rules acerted by it.

# Description

This project challenge us to know and work with the HTTP protocol in order to build a web server that receives and provides web calls, following the protocol guidelines to parse and control the connection sockets as well as the responses

We are also required to handle CGI calls, that basically run a program when the client calls a certain endpoint, that should be configured in the `.conf` file as `cgi enabled`.

On the configuration file we also are required to handle `auto index` locations, that provide a view of the files and directories available under the configured root directory, from there the client can browse the directory further down by clicking on the items displayed as links.

`Webserv` handles `GET`, `POST` and `DELETE` requests, and it sets a standard maximum body size if none is set on the `.conf` file in order to prevent runtime problems.

# Instructions

The project is built using Makefile with the usual flags:

- **make** : Builds the project
- **make debug** : Build with the `-g` flag for further debugging if required
- **make clean** : Removes the `.o` object files created by make
- **make fclean** : Removes the `.o` object and the `webserv` program
- **make re** : Fully cleans the directory of previously created files and builds again

Execute the `webserv` program created on this project root directory.

    When no argument is provided on `webserv` execution, the server will use the standard configuration file `config/example.conf`.

    You can use your own configuration file by providing the `.conf` file on execution: `webserv <you-config-file>.conf`

It will run on a terminal instance, so open another instance or a browser of you choice and access <hostname>:<port></><location> in order to receive your response:

## Requests Examples

All examples below follow the provided `config/example.conf`, which defines:
- `example.com` on port **8080** (also reachable on **8081**) — static files, uploads, a redirect and CGI.
- `vhost2.local` on the **same** port 8080 — routed by the `Host` header.
- `second.local` on port **8082** — used to demonstrate the body-size limit.

### In a browser (GET only)

A browser can only issue `GET`, so type `<hostname>:<port>/<location>` in the address bar:

- `localhost:8080/`
    serves the `index.html` set as `index` at the root `www/`.
- `localhost:8080/old`
    returns a `301` redirect pointing to the configured `/new` location.
- `localhost:8080/upload/`
    lists the directory contents (`autoindex on`); click an entry to browse further.
- `localhost:8080/cgi-bin/echo.py`
    runs the CGI script and returns its generated output.
- `localhost:8080/missing`
    returns `404` served with the custom error page configured by `error_page 404 /errors/404.html`
    (the server falls back to a built-in page if the configured file is missing).

### With curl (other methods and headers)

`POST`, `DELETE` and custom headers can't be sent from the address bar, so use `curl`:

- **Upload a file** (`POST`) — stored under `upload_store www/upload`:
```sh
curl -X POST --data-binary "hello webserv" http://localhost:8080/upload/hello.txt
```
- **Download the uploaded file** (`GET`):
```sh
curl http://localhost:8080/upload/hello.txt
```
- **Delete the uploaded file** (`DELETE`):
```sh
curl -X DELETE http://localhost:8080/upload/hello.txt
```
- **Method not allowed** — `location /` only permits `GET`, so this returns `405`:
```sh
curl -X DELETE http://localhost:8080/
```
- **CGI with a request body** — the body is passed to the script's stdin:
```sh
curl -X POST --data-binary "hello cgi" http://localhost:8080/cgi-bin/echo.py
```
- **Virtual host routing** — same port 8080, routed by the `Host` header to `vhost2.local` (a `301`):
```sh
curl -H "Host: vhost2.local" http://localhost:8080/
```
- **Body-size limit** — `second.local` on 8082 caps bodies at `1000` bytes, so an over-limit `POST` returns `413`:
```sh
curl -X POST --data-binary "$(python3 -c 'print("x"*2000)')" http://localhost:8082/anything
```
## Server Configuration File .conf

A server block is valid without locations. Location blocks are refinements of how specific URI prefixes are handled. So when a request arrives and no location is matched for it webserv handles it directly from a `server-level context`.

### Supported directives

**Server level**

| Directive | Syntax | Description |
|---|---|---|
| `listen` | `listen <port>` or `listen <host>:<port>` | Port (and optional bind address) the server listens on. At least one is required. |
| `server_name` | `server_name <name>` | Hostname used for virtual-host routing via the `Host` header. |
| `error_page` | `error_page <code> <path>` | Maps an HTTP error code to a custom error page file. |
| `client_max_body_size` | `client_max_body_size <bytes>` | Maximum allowed request body size in bytes. |

**Location level**

| Directive | Syntax | Description |
|---|---|---|
| `methods` | `methods <METHOD> [METHOD ...]` | Allowed HTTP methods (`GET`, `POST`, `DELETE`). |
| `root` | `root <path>` | Directory that serves as the document root for this location. |
| `index` | `index <file>` | Default file served when the request resolves to a directory. |
| `autoindex` | `autoindex on\|off` | Enables directory listing when no index file is found. |
| `upload_store` | `upload_store <path>` | Directory where uploaded files are stored (enables uploads). |
| `return` | `return <3xx> <url>` | Sends a redirect response with the given code and target URL. |
| `cgi` | `cgi <.ext> [interpreter]` | Registers a CGI extension and its interpreter (e.g. `cgi .py /usr/bin/python3`). |
| `cgi_timeout` | `cgi_timeout <seconds>` | Maximum seconds a CGI process may run before being killed (default: 10). |

### Configuration validation

Before starting, `webserv` validates the parsed configuration and exits with a clear error message if any of these checks fail:

- The configuration file must not be empty or unreadable.
- At least one `server` block must be defined.
- Every server must have at least one `listen` directive.
- Port numbers must be between 1 and 65535.
- Location paths must start with `/`.
- Methods must be one of `GET`, `POST`, or `DELETE`.
- `return` codes must be in the 3xx range.
- Duplicate location paths within the same server are not allowed.

