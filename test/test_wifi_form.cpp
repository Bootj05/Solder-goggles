#include <unity.h>
// Copyright 2025 Bootj05
#include <string>

static std::string renderWifiForm(const std::string &tpl,
                                  const std::string &ssid,
                                  const std::string &host) {
    std::string html = tpl;
    size_t pos;
    while ((pos = html.find("%SSID%")) != std::string::npos)
        html.replace(pos, 6, ssid);
    while ((pos = html.find("%HOST%")) != std::string::npos)
        html.replace(pos, 6, host);
    return html;
}

void test_wifi_form_hostname() {
    const std::string tpl =
        "<input id='ssid' value='%SSID%'><input id='host' value='%HOST%'>";
    std::string html = renderWifiForm(tpl, "mySSID", "MyDevice");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, html.find("MyDevice"));
}

