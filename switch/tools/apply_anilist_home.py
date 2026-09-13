from pathlib import Path
import re

main = Path("switch/source/main.cpp")
source = main.read_text()

pattern = re.compile(
    r"static ApiResult run_api_probe\(\)\n\{.*?\n\}\n\nstatic bool download_image",
    re.S,
)

replacement = r'''static ApiResult run_api_probe()
{
    ApiResult result;
    log_stage("BEFORE ANILIST HOME REQUEST");

    Result socketRc = socketInitializeDefault();
    bool socketOwned = false;
    if (R_SUCCEEDED(socketRc))
        socketOwned = true;
    else if (socketRc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
    {
        log_stage("ANILIST SOCKET INITIALIZE FAILED");
        result.status = "Network init failed";
        return result;
    }

    CURLcode globalRc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (globalRc != CURLE_OK)
    {
        log_stage("ANILIST CURL GLOBAL INIT FAILED");
        if (socketOwned) socketExit();
        result.status = "HTTP init failed";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        log_stage("ANILIST CURL EASY INIT FAILED");
        curl_global_cleanup();
        if (socketOwned) socketExit();
        result.status = "HTTP client init failed";
        return result;
    }

    const char* url = "https://graphql.anilist.co";
    const char* body = "{\"query\":\"query { Page(page: 1, perPage: 6) { pageInfo { hasNextPage } media(type: ANIME, sort: TRENDING_DESC) { id title { english romaji native } coverImage { large } format averageScore } } }\"}";
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(body)));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.3");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    log_stage("ANILIST HOME REQUEST");
    CURLcode requestRc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (requestRc == CURLE_OK && httpCode >= 200 && httpCode < 300 && response.find("\"media\"") != std::string::npos)
    {
        char marker[128];
        std::snprintf(marker, sizeof(marker), "ANILIST HOME REQUEST OK HTTP %ld BYTES %zu", httpCode, response.size());
        log_stage(marker);
        result.status = "AniList online - trending data received";
        result.response = response;
    }
    else
    {
        char marker[160];
        std::snprintf(marker, sizeof(marker), "ANILIST HOME REQUEST FAILED CURL %d HTTP %ld BYTES %zu", static_cast<int>(requestRc), httpCode, response.size());
        log_stage(marker);
        result.status = "AniList request failed - UI still running";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();
    log_stage("ANILIST HOME REQUEST CLEANUP COMPLETE");
    return result;
}

static bool download_image'''

matches = list(pattern.finditer(source))
if len(matches) != 1:
    raise SystemExit(f"Expected exactly one run_api_probe function, found {len(matches)}")

source = source[:matches[0].start()] + replacement + source[matches[0].end():]

# AniList returns the Home items under Page.media. Keep the renderer's
# existing object-walking logic, but make its collection anchor match the
# actual GraphQL response.
source = source.replace('response.find("\\\"results\\\"")', 'response.find("\\\"media\\\"")')

main.write_text(source)
print("Home API path now uses AniList only and parses Page.media responses")
