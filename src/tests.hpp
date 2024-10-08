#pragma once

#include "hello_imgui/hello_imgui_logger.h"

#include "http.hpp"
#include "json.hpp"
#include "partial_dict.hpp"
#include "variables.hpp"
#include "utils.hpp"

#include "cmath"
#include "cstdint"
#include "optional"
#include "string"
#include "unordered_map"
#include "utility"
#include "variant"

enum RequestBodyType : uint8_t {
    REQUEST_JSON,
    REQUEST_PLAIN,
    REQUEST_MULTIPART,
    REQUEST_OTHER,
};

using RequestBody = std::variant<std::string, MultiPartBody>;

template <RequestBodyType type> constexpr size_t request_body_index() noexcept {
    if constexpr (type == REQUEST_JSON) {
        return 0; // string
    }
    if constexpr (type == REQUEST_PLAIN) {
        return 0; // string
    }
    if constexpr (type == REQUEST_OTHER) {
        return 0; // string
    }
    if constexpr (type == REQUEST_MULTIPART) {
        return 1; // MultiPartBody
    }
}

template <RequestBodyType type> constexpr auto request_body_inplace_index() noexcept {
    return std::in_place_index<request_body_index<type>()>;
}

RequestBodyType request_body_type(const std::string& str) noexcept;

struct Request {
    RequestBodyType body_type = REQUEST_JSON;
    std::string other_content_type;
    RequestBody body = "";

    Cookies cookies;
    Parameters parameters;
    Headers headers;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    constexpr bool operator==(const Request& other) const noexcept {
        return this->body_type == other.body_type &&
               (this->body_type != REQUEST_OTHER || other.body_type != REQUEST_OTHER ||
                this->other_content_type == other.other_content_type) &&
               this->body == other.body && this->cookies == other.cookies &&
               this->headers == other.headers && this->parameters == other.parameters;
    }
};

enum ResponseBodyType : uint8_t {
    RESPONSE_ANY,
    RESPONSE_JSON,
    RESPONSE_HTML,
    RESPONSE_PLAIN,
    RESPONSE_OTHER,
};

struct Response {
    std::string status = "2XX"; // A string so user can get hints and write their own status code
    ResponseBodyType body_type = RESPONSE_ANY;
    std::string other_content_type;
    std::string body = "";

    Cookies cookies;
    Headers headers;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    constexpr bool operator==(const Response& other) const noexcept {
        return this->status == other.status &&
               (this->body_type != RESPONSE_OTHER || other.body_type != RESPONSE_OTHER ||
                this->other_content_type == other.other_content_type) &&
               this->body_type == other.body_type && this->body == other.body &&
               this->cookies == other.cookies && this->headers == other.headers;
    }
};

struct AuthBasic {
    std::string name;
    std::string password;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;
};

struct AuthBearerToken {
    std::string token;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;
};

using AuthVariant = std::variant<std::monostate, AuthBasic, AuthBearerToken>;

enum AuthType : uint8_t {
    AUTH_NONE = 0,
    AUTH_BASIC = 1,
    AUTH_BEARER_TOKEN = 2,
};
static const char* AuthTypeLabels[] = {
    reinterpret_cast<const char*>("None"),
    reinterpret_cast<const char*>("Basic"),
    reinterpret_cast<const char*>("Bearer Token"),
};

enum ClientSettingsFlags : uint16_t {
    CLIENT_NONE = 0,
    CLIENT_DYNAMIC = 1 << 0,
    CLIENT_KEEP_ALIVE = 1 << 1,
    CLIENT_COMPRESSION = 1 << 2,
    CLIENT_FOLLOW_REDIRECTS = 1 << 3,
    CLIENT_PROXY = 1 << 4,
};

struct ClientSettings {
    uint16_t flags = CLIENT_NONE;

    AuthVariant auth;

    std::string proxy_host;
    int proxy_port;
    AuthVariant proxy_auth;

    size_t seconds_timeout = 10;
    size_t test_reruns = 1;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    constexpr bool operator==(const ClientSettings& other) const noexcept {
        return this->flags == other.flags;
    }
};

enum TestFlags : uint8_t {
    TEST_NONE = 0,
    TEST_DISABLED = 1 << 0,
};

struct Test {
    size_t parent_id;
    size_t id;
    uint8_t flags;

    HTTPType type;
    std::string endpoint;

    Variables variables;
    Request request;
    Response response;

    std::optional<ClientSettings> cli_settings;

    std::string label() const noexcept;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;
};

void test_resolve_url_variables(const VariablesMap& parent_vars, Test* test) noexcept;

enum TestResultStatus {
    STATUS_OK,
    STATUS_WAITING,
    STATUS_RUNNING,
    STATUS_CANCELLED,
    STATUS_WARNING,
    STATUS_ERROR,
};
static const char* TestResultStatusLabels[] = {
    /* [STATUS_OK] = */ reinterpret_cast<const char*>("Ok"),
    /* [STATUS_WAITING] = */ reinterpret_cast<const char*>("Waiting"),
    /* [STATUS_RUNNING] = */ reinterpret_cast<const char*>("Running"),
    /* [STATUS_CANCELLED] = */ reinterpret_cast<const char*>("Cancelled"),
    /* [STATUS_WARNING] = */ reinterpret_cast<const char*>("Warning"),
    /* [STATUS_ERROR] = */ reinterpret_cast<const char*>("Error"),
};

struct TestResult {
    // Can be written and read from any thread
    copy_atomic<bool> running;
    // Main thread writes when stopping tests
    copy_atomic<TestResultStatus> status = STATUS_WAITING;

    // Written in draw thread
    bool selected = true;

    // Is info opened in a modal
    bool open = false;

    Test original_test;
    size_t test_result_idx;
    VariablesMap variables;

    std::optional<httplib::Result> http_result;

    // Written only in test_run threads
    std::string verdict = "";

    // Request
    std::string req_body;
    std::string req_content_type;
    std::string req_endpoint;
    httplib::Headers req_headers;

    std::string res_body;

    // Progress
    size_t progress_total = 0;
    size_t progress_current = 0;

    TestResult(const Test& _original_test, size_t _test_result_idx, bool _running,
               const VariablesMap& _variables) noexcept
        : running(_running), test_result_idx(_test_result_idx), original_test(_original_test),
          variables(_variables) {}
};

enum GroupFlags : uint8_t {
    GROUP_NONE = 0,
    GROUP_DISABLED = 1 << 0,
    GROUP_OPEN = 1 << 1,
};

struct Group {
    size_t parent_id;
    size_t id;
    uint8_t flags;

    std::string name;
    std::optional<ClientSettings> cli_settings;

    std::vector<size_t> children_ids;
    Variables variables;

    std::string label() const noexcept;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;
};

enum NestedTestType : uint8_t {
    TEST_VARIANT,
    GROUP_VARIANT,
};
using NestedTest = std::variant<Test, Group>;

constexpr bool nested_test_eq(const NestedTest* a, const NestedTest* b) noexcept {
    assert(a != nullptr);
    assert(b != nullptr);

    if (a->index() != b->index()) {
        return false;
    }

    switch (a->index()) {
    case TEST_VARIANT: {
        assert(std::holds_alternative<Test>(*a));
        assert(std::holds_alternative<Test>(*b));

        const auto& test_a = std::get<Test>(*a);
        const auto& test_b = std::get<Test>(*b);
        return test_a.endpoint == test_b.endpoint && test_a.type == test_b.type &&
               test_a.request == test_b.request && test_a.response == test_b.response &&
               test_a.cli_settings == test_b.cli_settings && test_a.variables == test_b.variables;
    } break;
    case GROUP_VARIANT:
        assert(std::holds_alternative<Group>(*a));
        assert(std::holds_alternative<Group>(*b));

        const auto& group_a = std::get<Group>(*a);
        const auto& group_b = std::get<Group>(*b);
        return group_a.name == group_b.name && group_a.cli_settings == group_b.cli_settings &&
               group_a.variables == group_b.variables;
        break;
    }

    assert(false && "Unreachable");
    return false;
}

bool test_comp(const std::unordered_map<size_t, NestedTest>& tests, size_t a_id, size_t b_id);

MultiPartBody request_multipart_convert_json(const nlohmann::json& json) noexcept;

template <RequestBodyType to_type> void request_body_convert(Test* test) noexcept {
    using nljson = nlohmann::json;

    if (test->request.body.index() == request_body_index<to_type>()) {
        // If it's the same type don't convert
        return;
    }

    if constexpr (to_type == REQUEST_JSON || to_type == REQUEST_PLAIN) {
        assert(std::holds_alternative<MultiPartBody>(test->request.body));
        MultiPartBody mpb = std::get<MultiPartBody>(test->request.body);

        std::unordered_multimap<std::string, MultiPartBodyData> map;
        for (const auto& elem : mpb.elements) {
            if (!(elem.flags & PARTIAL_DICT_ELEM_ENABLED)) {
                continue;
            }
            map.emplace(elem.key, elem.data.data);
        }

        nljson map_json = map;
        test->request.body = map_json.dump(4);
    }

    if constexpr (to_type == REQUEST_MULTIPART) {
        assert(std::holds_alternative<std::string>(test->request.body));
        std::string str = std::get<std::string>(test->request.body);

        nljson j = nljson::parse(str, nullptr, false);

        test->request.body = request_multipart_convert_json(j);
    }

    test->request.body_type = to_type;
}

// Prefer request_body output instead
ContentType request_content_type(const Request* request) noexcept;
httplib::Headers request_headers(
    const VariablesMap& vars, const Test* test,
    const std::unordered_map<std::string, std::string>* overload_cookies = nullptr) noexcept;
httplib::Params request_params(const VariablesMap& vars, const Test* test) noexcept;

struct RequestBodyResult {
    std::string content_type;
    std::string body;
};

RequestBodyResult request_body(const VariablesMap& vars, const Test* test) noexcept;
httplib::Headers response_headers(const VariablesMap& vars, const Test* test) noexcept;
ContentType response_content_type(const Response* response) noexcept;

using ClientSettingsVisitor = COPY_GETTER_VISITOR(cli_settings, cli_settings);
using SetClientSettingsVisitor = SETTER_VISITOR(cli_settings, std::optional<ClientSettings>);

using IDVisitor = COPY_GETTER_VISITOR(id, id);
using SetIDVisitor = SETTER_VISITOR(id, size_t);

using ParentIDVisitor = COPY_GETTER_VISITOR(parent_id, parent_id);
using SetParentIDVisitor = SETTER_VISITOR(parent_id, size_t);

using VariablesVisitor = GETTER_VISITOR(variables);

using LabelVisitor = COPY_GETTER_VISITOR(label(), label);
