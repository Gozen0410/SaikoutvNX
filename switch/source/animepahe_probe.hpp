#pragma once
#include <string>
#include <curl/curl.h>

struct AnimePaheProbeResult {
    bool ok = false;
    long http_code = 0;
    std::string response;
    std::string status;
};

static size_t animepahe_probe_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* out = static_cast<std::string*>(userdata);
    size_t bytes = size * nmemb;
    if (out->size() < 512 * 1024) out->append(ptr, std::min(bytes, 512 * 1024 - out->size()));
    return bytes;
}

static AnimePaheProbeResult animepahe_probe(const char* base_url) {
    AnimePaheProbeResult r;
    CURL* curl = curl_easy_init();
    if (!curl) { r.status = "AnimePahe CURL init failed"; return r; }
    std::string url = std::string(base_url) + "/api/airing";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.2");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, animepahe_probe_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.response);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.http_code);
    curl_easy_cleanup(curl);
    r.ok = rc == CURLE_OK && r.http_code >= 200 && r.http_code < 300 && !r.response.empty();
    r.status = r.ok ? "AnimePahe API online" : "AnimePahe API request failed";
    return r;
}
