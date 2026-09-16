#include "json.hpp"

#include <cctype>
#include <cstdint>

namespace
{
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void append_utf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80)
        out.push_back(static_cast<char>(cp));
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static size_t skip_ws(const std::string& s, size_t pos, size_t limit)
{
    while (pos < limit && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    return pos;
}

static bool read_string(const std::string& s, size_t quote, size_t limit, std::string& out)
{
    if (quote == std::string::npos || quote >= limit || s[quote] != '"') return false;
    out.clear();
    for (size_t i = quote + 1; i < limit; ++i)
    {
        const char c = s[i];
        if (c == '"') return true;
        if (c != '\\')
        {
            out.push_back(c);
            continue;
        }
        if (++i >= limit) return false;
        const char esc = s[i];
        switch (esc)
        {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                if (i + 4 >= limit) return false;
                uint32_t cp = 0;
                for (size_t d = 1; d <= 4; ++d)
                {
                    const int h = hex_digit(s[i + d]);
                    if (h < 0) return false;
                    cp = (cp << 4) | static_cast<uint32_t>(h);
                }
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < limit && s[i + 1] == '\\' && s[i + 2] == 'u')
                {
                    uint32_t lo = 0;
                    bool ok = true;
                    for (size_t d = 3; d <= 6; ++d)
                    {
                        const int h = hex_digit(s[i + d]);
                        if (h < 0) { ok = false; break; }
                        lo = (lo << 4) | static_cast<uint32_t>(h);
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                append_utf8(out, cp);
                break;
            }
            default: out.push_back(esc); break;
        }
    }
    return false;
}

static size_t find_key(const std::string& body, const char* key, size_t from, size_t limit)
{
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = body.find(needle, from);
    return (pos == std::string::npos || pos >= limit) ? std::string::npos : pos;
}

static size_t find_value_start(const std::string& body, size_t keyPos, size_t limit)
{
    if (keyPos == std::string::npos) return std::string::npos;
    const size_t colon = body.find(':', keyPos);
    if (colon == std::string::npos || colon >= limit) return std::string::npos;
    return skip_ws(body, colon + 1, limit);
}

static size_t find_balanced_end(const std::string& body, size_t start, size_t limit, char open, char close)
{
    if (start == std::string::npos || start >= limit || body[start] != open) return std::string::npos;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = start; i < limit; ++i)
    {
        const char c = body[i];
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == open) ++depth;
        else if (c == close && --depth == 0) return i;
    }
    return std::string::npos;
}
}

namespace json
{
std::string str(const std::string& body, const char* key, size_t from)
{
    const size_t keyPos = find_key(body, key, from, body.size());
    const size_t value = find_value_start(body, keyPos, body.size());
    if (value == std::string::npos || value >= body.size() || body[value] != '"') return {};
    std::string result;
    return read_string(body, value, body.size(), result) ? result : std::string();
}

long integer(const std::string& body, const char* key, long dflt, size_t from)
{
    const size_t keyPos = find_key(body, key, from, body.size());
    size_t value = find_value_start(body, keyPos, body.size());
    if (value == std::string::npos || value >= body.size()) return dflt;
    bool negative = false;
    if (body[value] == '-') { negative = true; ++value; }
    if (value >= body.size() || !std::isdigit(static_cast<unsigned char>(body[value]))) return dflt;
    long result = 0;
    while (value < body.size() && std::isdigit(static_cast<unsigned char>(body[value])))
    {
        result = result * 10 + (body[value] - '0');
        ++value;
    }
    return negative ? -result : result;
}

std::string object(const std::string& body, const char* key, size_t from)
{
    const size_t keyPos = find_key(body, key, from, body.size());
    const size_t value = find_value_start(body, keyPos, body.size());
    if (value == std::string::npos || value >= body.size() || body[value] != '{') return {};
    const size_t end = find_balanced_end(body, value, body.size(), '{', '}');
    if (end == std::string::npos) return {};
    return body.substr(value, end - value + 1);
}

std::vector<std::string> objects(const std::string& body, const char* key, size_t from)
{
    std::vector<std::string> result;
    const size_t keyPos = find_key(body, key, from, body.size());
    const size_t value = find_value_start(body, keyPos, body.size());
    if (value == std::string::npos || value >= body.size() || body[value] != '[') return result;

    const size_t arrayEnd = find_balanced_end(body, value, body.size(), '[', ']');
    if (arrayEnd == std::string::npos) return result;

    size_t cursor = value + 1;
    while (cursor < arrayEnd)
    {
        cursor = skip_ws(body, cursor, arrayEnd);
        if (cursor >= arrayEnd) break;
        if (body[cursor] != '{')
        {
            ++cursor;
            continue;
        }
        const size_t end = find_balanced_end(body, cursor, arrayEnd, '{', '}');
        if (end == std::string::npos) break;
        result.emplace_back(body.substr(cursor, end - cursor + 1));
        cursor = end + 1;
    }
    return result;
}
}
