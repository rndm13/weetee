#pragma once

#include "algorithm"
#include "cstdint"
#include "utils.hpp"

static const char* RequestHeadersLabels[] = {
    reinterpret_cast<const char*>("A-IM"),
    reinterpret_cast<const char*>("Accept"),
    reinterpret_cast<const char*>("Accept-Charset"),
    reinterpret_cast<const char*>("Accept-Datetime"),
    reinterpret_cast<const char*>("Accept-Encoding"),
    reinterpret_cast<const char*>("Accept-Language"),
    reinterpret_cast<const char*>("Access-Control-Request-Method"),
    reinterpret_cast<const char*>("Access-Control-Request-Headers"),
    reinterpret_cast<const char*>("Authorization"),
    reinterpret_cast<const char*>("Cache-Control"),
    reinterpret_cast<const char*>("Connection"),
    reinterpret_cast<const char*>("Content-Encoding"),
    reinterpret_cast<const char*>("Content-Length"),
    reinterpret_cast<const char*>("Content-MD5"),
    reinterpret_cast<const char*>("Content-Type"),
    reinterpret_cast<const char*>("Cookie"),
    reinterpret_cast<const char*>("Date"),
    reinterpret_cast<const char*>("Expect"),
    reinterpret_cast<const char*>("Forwarded"),
    reinterpret_cast<const char*>("From"),
    reinterpret_cast<const char*>("Host"),
    reinterpret_cast<const char*>("HTTP2-Settings"),
    reinterpret_cast<const char*>("If-Match"),
    reinterpret_cast<const char*>("If-Modified-Since"),
    reinterpret_cast<const char*>("If-None-Match"),
    reinterpret_cast<const char*>("If-Range"),
    reinterpret_cast<const char*>("If-Unmodified-Since"),
    reinterpret_cast<const char*>("Max-Forwards"),
    reinterpret_cast<const char*>("Origin"),
    reinterpret_cast<const char*>("Pragma"),
    reinterpret_cast<const char*>("Prefer"),
    reinterpret_cast<const char*>("Proxy-Authorization"),
    reinterpret_cast<const char*>("Range"),
    reinterpret_cast<const char*>("Referer"),
    reinterpret_cast<const char*>("TE"),
    reinterpret_cast<const char*>("Trailer"),
    reinterpret_cast<const char*>("Transfer-Encoding"),
    reinterpret_cast<const char*>("User-Agent"),
    reinterpret_cast<const char*>("Upgrade"),
    reinterpret_cast<const char*>("Via"),
    reinterpret_cast<const char*>("Warning"),
};
static const char* ResponseHeadersLabels[] = {
    reinterpret_cast<const char*>("Accept-CH"),
    reinterpret_cast<const char*>("Access-Control-Allow-Origin"),
    reinterpret_cast<const char*>("Access-Control-Allow-Credentials"),
    reinterpret_cast<const char*>("Access-Control-Expose-Headers"),
    reinterpret_cast<const char*>("Access-Control-Max-Age"),
    reinterpret_cast<const char*>("Access-Control-Allow-Methods"),
    reinterpret_cast<const char*>("Access-Control-Allow-Headers"),
    reinterpret_cast<const char*>("Accept-Patch"),
    reinterpret_cast<const char*>("Accept-Ranges"),
    reinterpret_cast<const char*>("Age"),
    reinterpret_cast<const char*>("Allow"),
    reinterpret_cast<const char*>("Alt-Svc"),
    reinterpret_cast<const char*>("Cache-Control"),
    reinterpret_cast<const char*>("Connection"),
    reinterpret_cast<const char*>("Content-Disposition"),
    reinterpret_cast<const char*>("Content-Encoding"),
    reinterpret_cast<const char*>("Content-Language"),
    reinterpret_cast<const char*>("Content-Length"),
    reinterpret_cast<const char*>("Content-Location"),
    reinterpret_cast<const char*>("Content-MD5"),
    reinterpret_cast<const char*>("Content-Range"),
    reinterpret_cast<const char*>("Content-Type"),
    reinterpret_cast<const char*>("Date"),
    reinterpret_cast<const char*>("Delta-Base"),
    reinterpret_cast<const char*>("ETag"),
    reinterpret_cast<const char*>("Expires"),
    reinterpret_cast<const char*>("IM"),
    reinterpret_cast<const char*>("Last-Modified"),
    reinterpret_cast<const char*>("Link"),
    reinterpret_cast<const char*>("Location"),
    reinterpret_cast<const char*>("P3P"),
    reinterpret_cast<const char*>("Pragma"),
    reinterpret_cast<const char*>("Preference-Applied"),
    reinterpret_cast<const char*>("Proxy-Authenticate"),
    reinterpret_cast<const char*>("Public-Key-Pins"),
    reinterpret_cast<const char*>("Retry-After"),
    reinterpret_cast<const char*>("Server"),
    reinterpret_cast<const char*>("Set-Cookie"),
    reinterpret_cast<const char*>("Strict-Transport-Security"),
    reinterpret_cast<const char*>("Trailer"),
    reinterpret_cast<const char*>("Transfer-Encoding"),
    reinterpret_cast<const char*>("Tk"),
    reinterpret_cast<const char*>("Upgrade"),
    reinterpret_cast<const char*>("Vary"),
    reinterpret_cast<const char*>("Via"),
    reinterpret_cast<const char*>("Warning"),
    reinterpret_cast<const char*>("WWW-Authenticate"),
    reinterpret_cast<const char*>("X-Frame-Options"),
};

enum HTTPType : uint8_t {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
};
static const char* HTTPTypeLabels[] = {
    /* [HTTP_GET] = */ reinterpret_cast<const char*>("GET"),
    /* [HTTP_POST] = */ reinterpret_cast<const char*>("POST"),
    /* [HTTP_PUT] = */ reinterpret_cast<const char*>("PUT"),
    /* [HTTP_DELETE] = */ reinterpret_cast<const char*>("DELETE"),
    /* [HTTP_PATCH] = */ reinterpret_cast<const char*>("PATCH"),
};
static ImVec4 HTTPTypeColor[] = {
    /* [HTTP_GET] = */ rgb_to_ImVec4(58, 142, 48, 255),
    /* [HTTP_POST] = */ rgb_to_ImVec4(160, 173, 64, 255),
    /* [HTTP_PUT] = */ rgb_to_ImVec4(181, 94, 65, 255),
    /* [HTTP_DELETE] = */ rgb_to_ImVec4(201, 61, 22, 255),
    /* [HTTP_PATCH] = */ rgb_to_ImVec4(99, 22, 90, 255),
};

bool http_type_button(HTTPType type) noexcept;
HTTPType http_type_from_label(std::string) noexcept;

static const char* HTTPStatusLabels[] = {
    reinterpret_cast<const char*>("100 Continue"),
    reinterpret_cast<const char*>("101 Switching Protocol"),
    reinterpret_cast<const char*>("102 Processing"),
    reinterpret_cast<const char*>("103 Early Hints"),
    reinterpret_cast<const char*>("200 OK"),
    reinterpret_cast<const char*>("201 Created"),
    reinterpret_cast<const char*>("202 Accepted"),
    reinterpret_cast<const char*>("203 Non-Authoritative Information"),
    reinterpret_cast<const char*>("204 No Content"),
    reinterpret_cast<const char*>("205 Reset Content"),
    reinterpret_cast<const char*>("206 Partial Content"),
    reinterpret_cast<const char*>("207 Multi-Status"),
    reinterpret_cast<const char*>("208 Already Reported"),
    reinterpret_cast<const char*>("226 IM Used"),
    reinterpret_cast<const char*>("300 Multiple Choices"),
    reinterpret_cast<const char*>("301 Moved Permanently"),
    reinterpret_cast<const char*>("302 Found"),
    reinterpret_cast<const char*>("303 See Other"),
    reinterpret_cast<const char*>("304 Not Modified"),
    reinterpret_cast<const char*>("305 Use Proxy"),
    reinterpret_cast<const char*>("306 unused"),
    reinterpret_cast<const char*>("307 Temporary Redirect"),
    reinterpret_cast<const char*>("308 Permanent Redirect"),
    reinterpret_cast<const char*>("400 Bad Request"),
    reinterpret_cast<const char*>("401 Unauthorized"),
    reinterpret_cast<const char*>("402 Payment Required"),
    reinterpret_cast<const char*>("403 Forbidden"),
    reinterpret_cast<const char*>("404 Not Found"),
    reinterpret_cast<const char*>("405 Method Not Allowed"),
    reinterpret_cast<const char*>("406 Not Acceptable"),
    reinterpret_cast<const char*>("407 Proxy Authentication Required"),
    reinterpret_cast<const char*>("408 Request Timeout"),
    reinterpret_cast<const char*>("409 Conflict"),
    reinterpret_cast<const char*>("410 Gone"),
    reinterpret_cast<const char*>("411 Length Required"),
    reinterpret_cast<const char*>("412 Precondition Failed"),
    reinterpret_cast<const char*>("413 Payload Too Large"),
    reinterpret_cast<const char*>("414 URI Too Long"),
    reinterpret_cast<const char*>("415 Unsupported Media Type"),
    reinterpret_cast<const char*>("416 Range Not Satisfiable"),
    reinterpret_cast<const char*>("417 Expectation Failed"),
    reinterpret_cast<const char*>("418 I'm a teapot"),
    reinterpret_cast<const char*>("421 Misdirected Request"),
    reinterpret_cast<const char*>("422 Unprocessable Content"),
    reinterpret_cast<const char*>("423 Locked"),
    reinterpret_cast<const char*>("424 Failed Dependency"),
    reinterpret_cast<const char*>("425 Too Early"),
    reinterpret_cast<const char*>("426 Upgrade Required"),
    reinterpret_cast<const char*>("428 Precondition Required"),
    reinterpret_cast<const char*>("429 Too Many Requests"),
    reinterpret_cast<const char*>("431 Request Header Fields Too Large"),
    reinterpret_cast<const char*>("451 Unavailable For Legal Reasons"),
    reinterpret_cast<const char*>("500 Internal Server Error"),
    reinterpret_cast<const char*>("501 Not Implemented"),
    reinterpret_cast<const char*>("502 Bad Gateway"),
    reinterpret_cast<const char*>("503 Service Unavailable"),
    reinterpret_cast<const char*>("504 Gateway Timeout"),
    reinterpret_cast<const char*>("505 HTTP Version Not Supported"),
    reinterpret_cast<const char*>("506 Variant Also Negotiates"),
    reinterpret_cast<const char*>("507 Insufficient Storage"),
    reinterpret_cast<const char*>("508 Loop Detected"),
    reinterpret_cast<const char*>("510 Not Extended"),
    reinterpret_cast<const char*>("511 Network Authentication Required"),
};

constexpr bool is_cookie_attribute(std::string key) noexcept {
    std::for_each(key.begin(), key.end(), [](char& c) { c = static_cast<char>(std::tolower(c)); });
    return key == "domain" || key == "expires" || key == "httponly" || key == "max-age" ||
           key == "partitioned" || key == "path" || key == "samesite" || key == "secure";
}

std::vector<std::string> parse_url_params(const std::string& endpoint) noexcept;

std::pair<std::string, std::string> split_endpoint(std::string endpoint) noexcept;

struct ContentType {
    std::string type;
    std::string name;

    constexpr bool operator!=(const ContentType& other) noexcept {
        if (this->type != "*" && other.type != "*" && other.type != this->type) {
            return true;
        }
        return this->name != "*" && other.name != "*" && other.name != this->name;
    }
};

std::string to_string(const ContentType& cont) noexcept;
ContentType parse_content_type(std::string input) noexcept;

bool status_match(const std::string& match, int status) noexcept;
