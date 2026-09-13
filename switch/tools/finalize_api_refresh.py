from pathlib import Path

main = Path("switch/source/main.cpp")
source = main.read_text()

# Gogoanime-only incremental integration. Keep provider IDs stable.
old_url = 'const char* url = "https://miruro.zenos.my.id/trending?per_page=6";'
if old_url not in source:
    raise SystemExit("Could not locate baseline provider URL")

new_url = '''const char* url = nullptr;
    const char* providerMarker = nullptr;
    switch (g_apiSource)
    {
        case 1:
            url = "https://animepahe.com/api?m=airing&page=1";
            providerMarker = "ANIMEPAHE REQUEST";
            break;
        case 2:
            url = "https://jaybeeanime.vercel.app/";
            providerMarker = "GOGOANIME REQUEST";
            break;
        default:
            url = "https://miruro.zenos.my.id/trending?per_page=6";
            providerMarker = "MIRURO REQUEST";
            break;
    }'''
source = source.replace(old_url, new_url, 1)

old_validation = 'primaryOk = api_response_is_valid(requestRc, httpCode, response, "MIRURO REQUEST");'
if old_validation not in source:
    raise SystemExit("Could not locate baseline provider validation")
source = source.replace(
    old_validation,
    '''if (g_apiSource == 2)
        {
            primaryOk = requestRc == CURLE_OK && httpCode >= 200 && httpCode < 300 && !response.empty();
            char providerStatus[160];
            std::snprintf(providerStatus, sizeof(providerStatus), "%s %s HTTP %ld BYTES %zu", providerMarker, primaryOk ? "OK" : "FAILED", httpCode, response.size());
            log_stage(providerStatus);
        }
        else
            primaryOk = api_response_is_valid(requestRc, httpCode, response, providerMarker);''',
    1,
)

# Alternate provider failure is terminal for this request; Miruro keeps its
# legacy AniList fallback behavior.
if "ALTERNATE PROVIDER FAILED - NO FALLBACK" not in source:
    marker = '    if (primaryOk)\n    {\n'
    guard = '''    if (!primaryOk && g_apiSource != 0)
    {
        log_stage("ALTERNATE PROVIDER FAILED - NO FALLBACK");
        result.status = std::string(api_source_name(g_apiSource)) + " request failed";
        result.response.clear();
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        if (socketOwned) socketExit();
        return result;
    }

'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate primary result branch")
    source = source.replace(marker, guard + marker, 1)

source = source.replace(
    '''    else\n    {\n        log_stage("MIRURO FAILED - STARTING ANILIST FALLBACK");''',
    '''    else if (g_apiSource == 0)\n    {\n        log_stage("MIRURO FAILED - STARTING ANILIST FALLBACK");''',
    1,
)

# Convert the simple Gogoanime root array to the existing renderer's expected
# results array. Generate valid C++ string literals directly.
if "static std::string normalize_gogoanime_response" not in source:
    marker = 'static std::vector<std::string> extract_trending_titles(const std::string& response)\n'
    helper = '''static std::string normalize_gogoanime_response(const std::string& response)
{
    if (response.empty() || response.front() != '[')
        return std::string();

    std::string out = "{\\\"results\\\":[";
    size_t cursor = 1;
    int count = 0;
    while (count < 6)
    {
        size_t start = response.find('{', cursor);
        if (start == std::string::npos) break;
        size_t end = response.find('}', start + 1);
        if (end == std::string::npos) break;
        if (count > 0) out += ',';
        out += response.substr(start, end - start + 1);
        ++count;
        cursor = end + 1;
    }
    out += "]}";
    return count > 0 ? out : std::string();
}

'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate trending parser")
    source = source.replace(marker, helper + marker, 1)

old_extract = '''    std::vector<std::string> titles = extract_trending_titles(response);\n    std::vector<std::string> details = extract_trending_details(response);\n    std::vector<std::string> covers = extract_trending_covers(response);'''
if old_extract not in source:
    raise SystemExit("Could not locate Home extraction block")

new_extract = '''    std::string normalizedResponse = g_apiSource == 2 ? normalize_gogoanime_response(response) : response;
    if (normalizedResponse.empty())
        return;
    std::vector<std::string> titles = extract_trending_titles(normalizedResponse);
    std::vector<std::string> details = extract_trending_details(normalizedResponse);
    std::vector<std::string> covers;
    if (g_apiSource == 2)
    {
        size_t cursor = normalizedResponse.find("\\\"results\\\"");
        while (covers.size() < 6 && cursor != std::string::npos)
        {
            size_t imagePos = normalizedResponse.find("\\\"image\\\"", cursor);
            if (imagePos == std::string::npos) break;
            size_t objectStart = normalizedResponse.rfind('{', imagePos);
            size_t objectEnd = normalizedResponse.find('}', imagePos);
            if (objectStart == std::string::npos || objectEnd == std::string::npos) break;
            covers.push_back(json_string_after(normalizedResponse, objectStart, "image", objectEnd));
            cursor = objectEnd + 1;
        }
    }
    else
        covers = extract_trending_covers(normalizedResponse);'''
source = source.replace(old_extract, new_extract, 1)

main.write_text(source)
print("Gogoanime generator emits valid C++ and routes provider id 2")
