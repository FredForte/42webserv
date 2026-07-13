#ifndef HTTP_RESPONSE_CODES_INDEX_HPP
#define HTTP_RESPONSE_CODES_INDEX_HPP

#include <map>
#include <string>

class HttpResponseCodesIndex {
	private:
		std::map<int, std::string> responseCodesDescriptions;

	public:
		HttpResponseCodesIndex();
		std::string getDescription(int code) const;
};

#endif
