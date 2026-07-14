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
