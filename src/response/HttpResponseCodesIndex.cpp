#include "../../include/response/HttpResponseCodesIndex.hpp"

HttpResponseCodesIndex::HttpResponseCodesIndex() {
	responseCodesDescriptions[200] = "OK";
    responseCodesDescriptions[500] = "Error";
}
