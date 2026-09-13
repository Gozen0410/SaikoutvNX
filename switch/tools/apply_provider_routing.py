from pathlib import Path
import re

main = Path("switch/source/main.cpp")
source = main.read_text()

old = '''const char* url = nullptr;\n    const char* providerMarker = nullptr;\n    if (g_apiSource == 1)\n    {\n        url = "https://animepahe.com/api?m=airing&page=1";\n        providerMarker = "ANIMEPAHE REQUEST";\n    }\n    else\n    {\n        url = "https://miruro.zenos.my.id/trending?per_page=6";\n        providerMarker = "MIRURO REQUEST";\n    }'''
new = '''const char* url = nullptr;\n    const char* providerMarker = nullptr;\n    switch (g_apiSource)\n    {\n        case 1:\n            url = "https://animepahe.com/api?m=airing&page=1";\n            providerMarker = "ANIMEPAHE REQUEST";\n            break;\n        case 2:\n            url = "https://jaybeeanime.vercel.app/";\n            providerMarker = "GOGOANIME REQUEST";\n            break;\n        default:\n            url = "https://miruro.zenos.my.id/trending?per_page=6";\n            providerMarker = "MIRURO REQUEST";\n            break;\n    }'''
if old in source:
    source = source.replace(old, new, 1)

old_valid = 'primaryOk = api_response_is_valid(requestRc, httpCode, response, "MIRURO REQUEST");'
new_valid = '''if (g_apiSource == 2)\n        {\n            primaryOk = requestRc == CURLE_OK && httpCode >= 200 && httpCode < 300 && !response.empty();\n            char providerStatus[160];\n            std::snprintf(providerStatus, sizeof(providerStatus), "%s %s HTTP %ld BYTES %zu", providerMarker, primaryOk ? "OK" : "FAILED", httpCode, response.size());\n            log_stage(providerStatus);\n        }\n        else\n            primaryOk = api_response_is_valid(requestRc, httpCode, response, providerMarker);'''
if old_valid in source:
    source = source.replace(old_valid, new_valid, 1)

if "ALTERNATE PROVIDER FAILED - NO FALLBACK" not in source:
    marker = '    if (primaryOk)\n    {\n'
    guard = '''    if (!primaryOk && g_apiSource != 0)\n    {\n        log_stage("ALTERNATE PROVIDER FAILED - NO FALLBACK");\n        result.status = std::string(api_source_name(g_apiSource)) + " request failed";\n        result.response.clear();\n        curl_easy_cleanup(curl);\n        curl_global_cleanup();\n        if (socketOwned) socketExit();\n        return result;\n    }\n\n'''
    if source.count(marker) == 1:
        source = source.replace(marker, guard + marker, 1)

source = source.replace('''    else\n    {\n        log_stage("MIRURO FAILED - STARTING ANILIST FALLBACK");''', '''    else if (g_apiSource == 0)\n    {\n        log_stage("MIRURO FAILED - STARTING ANILIST FALLBACK");''', 1)

# Gogoanime returns a root JSON array of {title,image,url} objects. Normalize
# it into the existing temporary {"results":[...]} shape used by the Home
# parser, avoiding a second renderer implementation.
if "static std::string normalize_gogoanime_response" not in source:
    marker = 'static std::vector<std::string> extract_trending_titles(const std::string& response)\n'
    helper = r'''static std::string normalize_gogoanime_response(const std::string& response)
{
    if (response.empty() || response.front() != '[')
        return std::string();

    std::string out = "{\"results\":[";
    size_t cursor = 1;
    int count = 0;
    while (count < 6)
    {
        size_t start = response.find('{', cursor);
        if (start == std::string::npos)
            break;
        size_t end = response.find('}', start + 1);
        if (end == std::string::npos)
            break;
        if (count > 0)
            out += ',';
        out += response.substr(start, end - start + 1);
        ++count;
        cursor = end + 1;
    }
    out += "]}";
    return count > 0 ? out : std::string();
}

'''
    if source.count(marker) == 1:
        source = source.replace(marker, helper + marker, 1)

# Ensure Gogoanime parsing uses the normalized response and valid C++ literals.
old_extract = '''    std::vector<std::string> titles = extract_trending_titles(response);\n    std::vector<std::string> details = extract_trending_details(response);\n    std::vector<std::string> covers = extract_trending_covers(response);'''
new_extract = '''    std::string normalizedResponse = g_apiSource == 2 ? normalize_gogoanime_response(response) : response;\n    if (normalizedResponse.empty())\n        return;\n\n    std::vector<std::string> titles = extract_trending_titles(normalizedResponse);\n    std::vector<std::string> details = extract_trending_details(normalizedResponse);\n    std::vector<std::string> covers;\n    if (g_apiSource == 2)\n    {\n        size_t cursor = normalizedResponse.find("\\\"results\\\"");\n        while (covers.size() < 6 && cursor != std::string::npos)\n        {\n            size_t imagePos = normalizedResponse.find("\\\"image\\\"", cursor);\n            if (imagePos == std::string::npos)\n                break;\n            size_t objectStart = normalizedResponse.rfind('{', imagePos);\n            size_t objectEnd = normalizedResponse.find('}', imagePos);\n            if (objectStart == std::string::npos || objectEnd == std::string::npos)\n                break;\n            covers.push_back(json_string_after(normalizedResponse, objectStart, "image", objectEnd));\n            cursor = objectEnd + 1;\n        }\n    }\n    else\n        covers = extract_trending_covers(normalizedResponse);'''
if old_extract in source:
    source = source.replace(old_extract, new_extract, 1)

# Never generate a duplicate API registry name helper. api_sources.hpp owns it.
source = re.sub(
    r'\nstatic const char\* api_source_name\(int source\)\n\{\n.*?\n\}\n',
    '\n',
    source,
    count=1,
    flags=re.S,
)

main.write_text(source)
print("Gogoanime routing compile escaping fixed; API registry helper deduplicated")
