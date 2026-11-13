#include "linfunc.h"

#include <algorithm>
#include <string>
#include <ctype.h>
#include <sstream>

// apply lowercase without duplicating, used only internally
char *inPlaceLowerCase(char *str)
{
    char *p = str;

    for (; *p; p++)
        *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));

    return str;
}

void inPlaceLowerCase(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
}

std::string lowercase(std::string str)
{
    inPlaceLowerCase(str);
    return str;
}

bool cstr_ci_equals(const char* const a, const char* const b)
{
    if (a == b)
        return true;

    for (const char *pa = a, *pb = b; *pa; pa++, pb++) {
        if (::tolower(*pa) != ::tolower(*pb))
            return false;
    }

    return true;
}

std::list<std::string_view> tokenize(std::string_view text, char delim, size_t maxsplit)
{
    if (text.empty()) {
        return { text };
    }

    std::list<std::string_view> tokens;
    size_t start = 0;

    while (start < text.size()) {
        if (maxsplit == 0) {
            tokens.emplace_back(text.substr(start));
            break;
        }

        const size_t end = text.find(delim, start);
        if (end == std::string_view::npos) {
            tokens.emplace_back(text.substr(start));
            break;
        }

        tokens.emplace_back(text.substr(start, end - start));
        start = end + 1;
        maxsplit--;
    }

    return tokens;
}
