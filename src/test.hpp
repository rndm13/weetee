#pragma once

#include "hello_imgui/hello_imgui_logger.h"

#include "imgui.h"

#include "httplib.h"

#include "nlohmann/json.hpp"

#include "http.hpp"
#include "json.hpp"
#include "partial_dict.hpp"
#include "utils.hpp"

#include "algorithm"
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
};
static const char* RequestBodyTypeLabels[] = {
    /* [REQUEST_JSON] = */ reinterpret_cast<const char*>("JSON"),
    /* [REQUEST_RAW] = */ reinterpret_cast<const char*>("Plain Text"),
    /* [REQUEST_MULTIPART] = */ reinterpret_cast<const char*>("Multipart"),
};

using RequestBody = std::variant<std::string, MultiPartBody>;

template <RequestBodyType type> constexpr size_t request_body_index() noexcept {
    if constexpr (type == REQUEST_JSON) {
        return 0; // string
    }
    if constexpr (type == REQUEST_PLAIN) {
        return 0; // string
    }
    if constexpr (type == REQUEST_MULTIPART) {
        return 1; // MultiPartBody
    }
}

template <RequestBodyType type> constexpr auto request_body_inplace_index() noexcept {
    return std::in_place_index<request_body_index<type>()>;
}

struct Request {
    RequestBodyType body_type = REQUEST_JSON;
    RequestBody body = "";

    Cookies cookies;
    Parameters parameters;
    Parameters url_parameters; // can only be added/removed when endpoint changes
    Headers headers;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    constexpr bool operator==(const Request& other) const noexcept {
        return this->body_type == other.body_type && this->body == other.body &&
               this->cookies == other.cookies && this->headers == other.headers &&
               this->parameters == other.parameters;
    }
};

enum ResponseBodyType : uint8_t {
    RESPONSE_JSON,
    RESPONSE_HTML,
    RESPONSE_PLAIN,
};
static const char* ResponseBodyTypeLabels[] = {
    /* [RESPONSE_JSON] = */ reinterpret_cast<const char*>("JSON"),
    /* [RESPONSE_HTML] = */ reinterpret_cast<const char*>("HTML"),
    /* [RESPONSE_RAW] = */ reinterpret_cast<const char*>("Plain Text"),
};

using ResponseBody =
    std::variant<std::string>; // probably will need to add file responses so will keep it this way

struct Response {
    std::string status = "2XX"; // a string so user can get hints and write their own status code
    ResponseBodyType body_type = RESPONSE_JSON;
    ResponseBody body = "";

    Cookies cookies;
    Headers headers;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    constexpr bool operator==(const Response& other) const noexcept {
        return this->status == other.status && this->body_type == other.body_type &&
               this->body == other.body && this->cookies == other.cookies &&
               this->headers == other.headers;
    }
};

enum ClientSettingsFlags : uint8_t {
    CLIENT_NONE = 0,
    CLIENT_DYNAMIC = 1 << 0,
    CLIENT_KEEP_ALIVE = 1 << 1,
    CLIENT_COMPRESSION = 1 << 2,
    CLIENT_FOLLOW_REDIRECTS = 1 << 3,
};

struct ClientSettings {
    uint8_t flags = CLIENT_NONE;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    constexpr bool operator==(const ClientSettings& other) const noexcept {
        return this->flags == other.flags;
    }
};

bool show_client_settings(ClientSettings* set) noexcept;

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

    Request request;
    Response response;

    std::optional<ClientSettings> cli_settings;

    std::string label() const noexcept;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;
};

std::string request_endpoint(const Test* test) noexcept;

enum TestResultStatus {
    STATUS_RUNNING,
    STATUS_CANCELLED,
    STATUS_OK,
    STATUS_ERROR,
    STATUS_WARNING,
};
static const char* TestResultStatusLabels[] = {
    /* [STATUS_RUNNING] = */ reinterpret_cast<const char*>("Running"),
    /* [STATUS_CANCELLED] = */ reinterpret_cast<const char*>("Cancelled"),
    /* [STATUS_OK] = */ reinterpret_cast<const char*>("Ok"),
    /* [STATUS_ERROR] = */ reinterpret_cast<const char*>("Error"),
    /* [STATUS_WARNING] = */ reinterpret_cast<const char*>("Warning"),
};

struct TestResult {
    // can be written and read from any thread
    std::atomic_bool running = true;
    // main thread writes when stopping tests
    std::atomic<TestResultStatus> status = STATUS_RUNNING;

    // written in draw thread
    bool selected = true;

    // open info in a modal
    bool open;

    Test original_test;
    std::optional<httplib::Result> http_result;

    // written only in test_run threads
    std::string verdict = "0%";

    // progress
    size_t progress_total;
    size_t progress_current;

    TestResult(const Test& _original_test) noexcept : original_test(_original_test) {}
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
               test_a.cli_settings == test_b.cli_settings;
    } break;
    case GROUP_VARIANT:
        assert(std::holds_alternative<Group>(*a));
        assert(std::holds_alternative<Group>(*b));

        const auto& group_a = std::get<Group>(*a);
        const auto& group_b = std::get<Group>(*b);
        return group_a.name == group_b.name && group_a.cli_settings == group_b.cli_settings;
        break;
    }

    // unreachable
    return false;
}

bool test_comp(const std::unordered_map<size_t, NestedTest>& tests, size_t a_id, size_t b_id);

template <RequestBodyType to_type> void request_body_convert(Test* test) noexcept {
    if (test->request.body.index() == request_body_index<to_type>()) {
        // If it's the same type don't convert
        return;
    }

    if constexpr (to_type == REQUEST_JSON || to_type == REQUEST_PLAIN) {
        assert(std::holds_alternative<MultiPartBody>(test->request.body));
        MultiPartBody mpb = std::get<MultiPartBody>(test->request.body);

        std::unordered_multimap<std::string, MultiPartBodyData> map;
        for (const auto& elem : mpb.elements) {
            if (!elem.enabled) {
                continue;
            }
            map.emplace(elem.key, elem.data.data);
        }

        nlohmann::json json = map;
        test->request.body = json.dump(4);
    }

    if constexpr (to_type == REQUEST_MULTIPART) {
        assert(std::holds_alternative<std::string>(test->request.body));
        std::string str = std::get<std::string>(test->request.body);

        MultiPartBody to_replace = {};
        try {
            nlohmann::json j = nlohmann::json::parse(str);
            auto map = j.template get<std::unordered_multimap<std::string, MultiPartBodyData>>();

            for (const auto& [key, value] : map) {
                auto new_elem = MultiPartBodyElement{
                    .key = key,
                    .data =
                        MultiPartBodyElementData{
                            .type = static_cast<MultiPartBodyDataType>(value.index()),
                            .data = value,
                        },
                };
                to_replace.elements.push_back(new_elem);
            }
        } catch (std::exception& e) {
            Log(LogLevel::Error, "Failed to convert request body type: %s", e.what());
        }

        test->request.body = to_replace;
    }

    test->request.body_type = to_type;
}
