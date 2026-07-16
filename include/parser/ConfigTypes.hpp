#ifndef CONFIG_TYPES_HPP
#define CONFIG_TYPES_HPP

#include <map>
#include <string>
#include <vector>

struct ListenAddr {
	std::string host;
	int port;
};

// path : route prefix this block if for : ex.: "/upload"
// methods : vector with the methods provided on config for this path
// root : directory designated for this locaiton config
// index : file served when the request resolves to a director
// autoindex : whether to generate a directory listing when no index is provided
// upload_enabled : if this block should accept file uploads, is flagged when upload_store is found on the .conf
// upload_store : directory where uploaded files get written
// redirect_code : 0 means no redirect is configured for this block
// redirect_target : destination used when redirect_code != 0
// cgi_extensions : map with the extensions and path for the cgi executors
struct LocationConfig {
	std::string path;
	std::vector<std::string> methods;
	std::string root;
	std::string index;
	bool autoindex;
	bool upload_enabled;
	std::string upload_store;
	int redirect_code;
	std::string redirect_target;
	std::map<std::string, std::string> cgi_extensions;
};

// listens : we will consume all the connections that are going to be saved on this vector
// error_pages : a map to gather our configured page for each type of error for now, we can
// make a dynamic page and dispay a different message for each error.
// locations : the configurations we parsed from the .config file on a vector
struct ServerConfig {
	std::vector<ListenAddr> listens;
	std::string server_name;
	std::map<int, std::string> error_pages;
	size_t client_max_body_size;
	std::vector<LocationConfig> locations;
};

// Our main vector containing all the servers configured on .conf
typedef std::vector<ServerConfig> Config;

#endif
