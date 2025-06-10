#include <unity.h>
// Copyright 2025 Bootj05
#include <string>

static std::string storedSSID;      // NOLINT(runtime/string)
static std::string storedHostname;  // NOLINT(runtime/string)
static bool loadCalled;

void loadCredentials() {
    loadCalled = true;
}

struct DummyServer {
    std::string body;
    void send(int /*code*/, const char* /*type*/, const std::string& html) {
        body = html;
    }
};

static DummyServer server;

const char WIFI_FORM_HTML[] =
    "<input id='ssid' value='%SSID%'><input id='host' value='%HOST%'>";

void handleWifiForm() {
    loadCredentials();
    std::string html = WIFI_FORM_HTML;
    size_t pos = html.find("%SSID%");
    if (pos != std::string::npos)
        html.replace(pos, 6, storedSSID);
    pos = html.find("%HOST%");
    if (pos != std::string::npos)
        html.replace(pos, 6, storedHostname);
    server.send(200, "text/html", html);
}

void test_wifi_form_hostname() {
    storedSSID = "MyNet";
    storedHostname = "MyGoggles";
    loadCalled = false;
    handleWifiForm();
    TEST_ASSERT_TRUE(loadCalled);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          server.body.find(storedHostname));
}

