// The parts of PortMapping.h that don't need the network: URLs, SSDP replies,
// the router's description, HTTP and SOAP text.

#include "PortMapping.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace rollback::upnp::detail {
namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        b--;
    return s.substr(a, b - a);
}

// The position of the next opening tag <tag> or <ns:tag ...> from `from` (npos: none); `end` gets
// the position after its '>'
size_t findTag(const std::string& xml, const std::string& tag, size_t from, size_t& end)
{
    for (size_t p = xml.find('<', from); p != std::string::npos; p = xml.find('<', p + 1)) {
        size_t name = p + 1;
        if (name < xml.size() && (xml[name] == '/' || xml[name] == '?' || xml[name] == '!'))
            continue;
        //(skip a namespace prefix)
        size_t q = name;
        while (q < xml.size() && xml[q] != '>' && xml[q] != ' ' && xml[q] != ':' && xml[q] != '/')
            q++;
        if (q < xml.size() && xml[q] == ':')
            name = q + 1;
        if (xml.compare(name, tag.size(), tag) != 0)
            continue;
        const size_t after = name + tag.size();
        if (after >= xml.size() || (xml[after] != '>' && xml[after] != ' ' && xml[after] != '/' && xml[after] != '\t' && xml[after] != '\n' && xml[after] != '\r'))
            continue;
        const size_t close = xml.find('>', after);
        if (close == std::string::npos)
            return std::string::npos;
        end = close + 1;
        return p;
    }
    return std::string::npos;
}

// The position of the closing tag </tag> or </ns:tag> from `from`
size_t findClose(const std::string& xml, const std::string& tag, size_t from)
{
    for (size_t p = xml.find("</", from); p != std::string::npos; p = xml.find("</", p + 2)) {
        size_t name = p + 2;
        size_t q = name;
        while (q < xml.size() && xml[q] != '>' && xml[q] != ':')
            q++;
        if (q < xml.size() && xml[q] == ':')
            name = q + 1;
        if (xml.compare(name, tag.size(), tag) == 0 && name + tag.size() < xml.size() && xml[name + tag.size()] == '>')
            return p;
    }
    return std::string::npos;
}

// "http://host:port" of a URL
std::string origin(const Url& u)
{
    return "http://" + u.host + ":" + std::to_string(u.port);
}

}  // namespace

bool parseUrl(const std::string& url, Url& out)
{
    const std::string scheme = "http://";
    if (lower(url.substr(0, scheme.size())) != scheme)
        return false;
    const size_t hostStart = scheme.size();
    size_t slash = url.find('/', hostStart);
    const std::string authority = url.substr(hostStart, slash == std::string::npos ? std::string::npos : slash - hostStart);
    out = Url();
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        out.host = authority.substr(0, colon);
        const int port = std::atoi(authority.c_str() + colon + 1);
        if (port <= 0 || port > 65535)
            return false;
        out.port = static_cast<uint16_t>(port);
    } else {
        out.host = authority;
    }
    out.path = slash == std::string::npos ? "/" : url.substr(slash);
    return !out.host.empty();
}

std::string ssdpLocation(const std::string& reply)
{
    size_t start = 0;
    while (start < reply.size()) {
        size_t end = reply.find('\n', start);
        if (end == std::string::npos)
            end = reply.size();
        const std::string line = reply.substr(start, end - start);
        const size_t colon = line.find(':');
        if (colon != std::string::npos && lower(trim(line.substr(0, colon))) == "location")
            return trim(line.substr(colon + 1));
        start = end + 1;
    }
    return {};
}

std::string xmlValue(const std::string& xml, const std::string& tag)
{
    size_t end = 0;
    const size_t open = findTag(xml, tag, 0, end);
    if (open == std::string::npos || xml[end - 2] == '/')
        return {};
    const size_t close = findClose(xml, tag, end);
    if (close == std::string::npos)
        return {};
    return trim(xml.substr(end, close - end));
}

std::vector<Service> wanServices(const std::string& descriptionXml, const std::string& descriptionUrl)
{
    std::vector<Service> services;
    Url from;
    if (!parseUrl(descriptionUrl, from))
        return services;
    std::string base = xmlValue(descriptionXml, "URLBase");
    Url baseUrl;
    if (base.empty() || !parseUrl(base, baseUrl))
        baseUrl = from;

    size_t pos = 0;
    for (;;) {
        size_t end = 0;
        const size_t open = findTag(descriptionXml, "service", pos, end);
        if (open == std::string::npos)
            break;
        const size_t close = findClose(descriptionXml, "service", end);
        if (close == std::string::npos)
            break;
        const std::string block = descriptionXml.substr(end, close - end);
        pos = close + 1;

        Service s;
        s.type = xmlValue(block, "serviceType");
        if (s.type.find("WANIPConnection") == std::string::npos && s.type.find("WANPPPConnection") == std::string::npos)
            continue;
        std::string control = xmlValue(block, "controlURL");
        if (control.empty())
            continue;
        if (lower(control.substr(0, 7)) == "http://")
            s.controlUrl = control;
        else if (control[0] == '/')
            s.controlUrl = origin(baseUrl) + control;
        else {
            std::string dir = baseUrl.path;
            dir = dir.substr(0, dir.rfind('/') + 1);
            s.controlUrl = origin(baseUrl) + (dir.empty() ? "/" : dir) + control;
        }
        services.push_back(s);
    }
    //(IP connections first: PPP ones are rarely the active one)
    std::stable_sort(services.begin(), services.end(), [](const Service& a, const Service& b) {
        return (a.type.find("WANIPConnection") != std::string::npos) > (b.type.find("WANIPConnection") != std::string::npos);
    });
    return services;
}

std::string httpBody(const std::string& response, int& status)
{
    status = 0;
    if (response.compare(0, 5, "HTTP/") != 0)
        return {};
    const size_t space = response.find(' ');
    if (space != std::string::npos)
        status = std::atoi(response.c_str() + space + 1);
    size_t headersEnd = response.find("\r\n\r\n");
    size_t bodyStart = headersEnd == std::string::npos ? std::string::npos : headersEnd + 4;
    if (headersEnd == std::string::npos) {
        headersEnd = response.find("\n\n");
        bodyStart = headersEnd == std::string::npos ? std::string::npos : headersEnd + 2;
    }
    if (bodyStart == std::string::npos)
        return {};
    const std::string headers = lower(response.substr(0, headersEnd));
    const std::string body = response.substr(bodyStart);
    if (headers.find("transfer-encoding: chunked") == std::string::npos && headers.find("transfer-encoding:chunked") == std::string::npos)
        return body;

    //Chunked: hex size, CRLF, data, CRLF ... until a size of 0
    std::string out;
    size_t p = 0;
    while (p < body.size()) {
        const size_t lineEnd = body.find("\r\n", p);
        if (lineEnd == std::string::npos)
            break;
        const unsigned long size = std::strtoul(body.substr(p, lineEnd - p).c_str(), nullptr, 16);
        if (size == 0)
            break;
        const size_t data = lineEnd + 2;
        if (data + size > body.size()) {
            out += body.substr(data);
            break;
        }
        out += body.substr(data, size);
        p = data + size + 2;
    }
    return out;
}

std::string soapEnvelope(const std::string& serviceType, const std::string& action,
    const std::vector<std::pair<std::string, std::string>>& args)
{
    std::string s = "<?xml version=\"1.0\"?>\r\n"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                    "<s:Body><u:" + action + " xmlns:u=\"" + serviceType + "\">";
    for (const auto& [name, value] : args)
        s += "<" + name + ">" + value + "</" + name + ">";
    s += "</u:" + action + "></s:Body></s:Envelope>\r\n";
    return s;
}

bool isPrivateIPv4(const std::string& ip)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return false;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127) ||
           (a == 169 && b == 254) || a == 127 || a == 0;
}

}  // namespace rollback::upnp::detail
