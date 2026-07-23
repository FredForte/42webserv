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

Requests Examples following the provided `example.conf`:
- localhost:8080/
    opens the `index.html` that is set at the root `www/`
- localhost:8080/old
    receives a redirect message pointing the configured directory
