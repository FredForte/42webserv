#ifndef HTTP_RESPONSE_CODES_INDEX_HPP
#define HTTP_RESPONSE_CODES_INDEX_HPP

#include <map>

class HttpResponseCodesIndex {
	public:
		std::map<int, std::string> responseCodesDescriptions;
		HttpResponseCodesIndex();
};	

#endif