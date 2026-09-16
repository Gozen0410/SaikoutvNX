#pragma once

#include <string>
#include <vector>

namespace json
{
std::string str(const std::string& body, const char* key, size_t from = 0);
long integer(const std::string& body, const char* key, long dflt = 0, size_t from = 0);
std::string object(const std::string& body, const char* key, size_t from = 0);
std::vector<std::string> objects(const std::string& body, const char* key, size_t from = 0);
}
