/*
 * VoidLink T-Dongle USB Adapter
 *
 * Host-facing USB NCM pairing appliance for ESP32-S3 native USB boards.
 * The host receives an address from the dongle and opens http://192.168.4.1/.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "usb_netif.h"

#define VOIDLINK_VERSION "0.2.1"
#define VOIDLINK_URL "http://192.168.4.1/"

static const char *TAG = "voidlink";

typedef struct {
    bool pairing_active;
    bool paired;
    bool deck_support_enabled;
    bool deck_detected;
    uint32_t pair_code;
    uint32_t event_count;
    char latest_action[64];
} voidlink_state_t;

static httpd_handle_t s_web_server = NULL;
static SemaphoreHandle_t s_state_lock;
static voidlink_state_t s_state = {
    .pairing_active = false,
    .paired = false,
    .deck_support_enabled = false,
    .deck_detected = false,
    .pair_code = 0,
    .event_count = 0,
    .latest_action = "boot",
};

static void state_update(const char *latest_action)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.event_count++;
    snprintf(s_state.latest_action, sizeof(s_state.latest_action), "%s", latest_action);
    xSemaphoreGive(s_state_lock);
}

static void set_common_headers(httpd_req_t *req, const char *content_type)
{
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    set_common_headers(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html lang=\"en\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>VoidLink T-Deck Pairing</title>"
        "<style>"
        ":root{color-scheme:dark;--bg:#05080f;--panel:#111827;--line:#284157;--text:#eef7ff;--muted:#a4b4c4;--cyan:#39f6d7;--green:#7cf29a;--amber:#ffd36a}"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:linear-gradient(135deg,rgba(57,246,215,.14),transparent 34%),linear-gradient(315deg,rgba(106,164,255,.12),transparent 40%),var(--bg);color:var(--text);font-family:Inter,system-ui,Segoe UI,sans-serif}"
        "main{width:min(920px,calc(100vw - 28px));margin:0 auto;padding:26px 0 32px}"
        "header{min-height:36vh;display:flex;flex-direction:column;justify-content:center;border-bottom:1px solid rgba(57,246,215,.3);gap:16px}"
        ".eyebrow{margin:0;color:var(--cyan);font-size:12px;font-weight:900;letter-spacing:.14em;text-transform:uppercase}"
        "h1{margin:0;font-size:clamp(42px,9vw,82px);line-height:.95;letter-spacing:0}p{color:var(--muted);line-height:1.55}"
        ".grid{display:grid;grid-template-columns:1.05fr .95fr;gap:14px;margin-top:16px}.panel{border:1px solid rgba(57,246,215,.22);border-radius:8px;background:rgba(17,24,39,.88);padding:16px}"
        ".stats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.stat{border:1px solid rgba(255,255,255,.1);border-radius:8px;padding:12px;background:#070b13}.stat span{display:block;color:var(--muted);font-size:12px;text-transform:uppercase}.stat strong{display:block;margin-top:6px;color:var(--green)}"
        ".actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:12px}button{min-height:44px;border:1px solid rgba(57,246,215,.4);border-radius:8px;background:#07131a;color:var(--text);font-weight:850;cursor:pointer}.primary{background:linear-gradient(90deg,var(--cyan),var(--green));color:#031016}.warn{border-color:rgba(255,211,106,.5);color:var(--amber)}"
        ".flow{margin:8px 0 0;padding-left:20px;color:var(--muted);line-height:1.55}.flow strong{color:var(--text)}"
        "pre{min-height:160px;overflow:auto;border:1px solid rgba(57,246,215,.18);border-radius:8px;background:#03060b;color:#bdfcf1;padding:12px;white-space:pre-wrap}"
        "@media(max-width:760px){.grid,.stats,.actions{grid-template-columns:1fr}button{width:100%}}"
        "</style></head><body><main>"
        "<header><p class=\"eyebrow\">USB network adapter UI</p><h1>VoidLink T-Deck Pairing</h1>"
        "<p>This page is served by the T-Dongle over USB Ethernet. It is the pairing/control surface: no separate T-Deck firmware menu is required for the computer-side setup.</p></header>"
        "<section class=\"grid\"><article class=\"panel\"><p class=\"eyebrow\">Status</p><div class=\"stats\">"
        "<div class=\"stat\"><span>USB URL</span><strong>192.168.4.1</strong></div>"
        "<div class=\"stat\"><span>Mode</span><strong>Dongle web UI</strong></div>"
        "<div class=\"stat\"><span>Host</span><strong>DHCP over NCM</strong></div>"
        "<div class=\"stat\"><span>Version</span><strong>" VOIDLINK_VERSION "</strong></div>"
        "</div><ol class=\"flow\"><li><strong>Enable T-Deck support</strong> on this dongle.</li><li><strong>Select T-Deck USB</strong> so the browser confirms a T-Deck-class ESP32-S3 is plugged into the computer.</li><li><strong>Begin Pair</strong>, then Confirm after the code is accepted.</li></ol>"
        "<div class=\"actions\"><button class=\"primary\" id=\"support\">Enable T-Deck support</button><button id=\"deck\">Select T-Deck USB</button><button id=\"begin\">Begin Pair</button><button id=\"confirm\">Confirm</button><button class=\"warn\" id=\"reset\">Reset Pairing</button><button id=\"off\">Disable Support</button></div></article>"
        "<article class=\"panel\"><p class=\"eyebrow\">Readout</p><pre id=\"out\">loading...</pre></article></section>"
        "<script>"
        "const out=document.getElementById('out');"
        "let deckInfo=null;"
        "async function api(path,opts){const r=await fetch(path,Object.assign({cache:'no-store'},opts||{})); if(!r.ok) throw new Error(r.status+' '+r.statusText); return r.json();}"
        "function render(s,extra){out.textContent=JSON.stringify(s,null,2)+(extra?\"\\n\\n\"+extra:\"\")+\"\\n\\nNext: \"+s.nextAction;}"
        "async function refresh(){try{render(await api('/api/status'));}catch(e){out.textContent='status failed: '+e.message;}}"
        "document.getElementById('support').onclick=async()=>render(await api('/api/deck-support-on',{method:'POST'}));"
        "document.getElementById('off').onclick=async()=>render(await api('/api/deck-support-off',{method:'POST'}));"
        "document.getElementById('deck').onclick=async()=>{try{if(!('serial'in navigator))throw new Error('Web Serial is not available in this browser');const p=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});deckInfo=p.getInfo();const s=await api('/api/deck-detected',{method:'POST'});render(s,'Selected USB VID 0x'+(deckInfo.usbVendorId||0).toString(16)+' PID 0x'+(deckInfo.usbProductId||0).toString(16)+'. Port was not opened, so Meshtastic serial stays free.');}catch(e){out.textContent='T-Deck USB selection failed: '+e.message+'\\nUse Chrome or Edge and pick the T-Deck app/bootloader port.';}};"
        "document.getElementById('begin').onclick=async()=>render(await api('/api/pair-begin',{method:'POST'}));"
        "document.getElementById('confirm').onclick=async()=>render(await api('/api/pair-confirm',{method:'POST'}));"
        "document.getElementById('reset').onclick=async()=>render(await api('/api/pair-reset',{method:'POST'}));"
        "refresh(); setInterval(refresh,3000);"
        "</script></main></body></html>";

    set_common_headers(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[768];

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool pairing_active = s_state.pairing_active;
    const bool paired = s_state.paired;
    const bool deck_support_enabled = s_state.deck_support_enabled;
    const bool deck_detected = s_state.deck_detected;
    const uint32_t pair_code = s_state.pair_code;
    const uint32_t event_count = s_state.event_count;
    char latest_action[sizeof(s_state.latest_action)];
    snprintf(latest_action, sizeof(latest_action), "%s", s_state.latest_action);
    xSemaphoreGive(s_state_lock);

    snprintf(json, sizeof(json),
             "{\"name\":\"VoidLink T-Dongle USB Adapter\","
             "\"version\":\"%s\","
             "\"mode\":\"usb-ncm-pairing\","
             "\"url\":\"%s\","
             "\"transport\":\"USB NCM\","
             "\"hostAddress\":\"192.168.4.2\","
             "\"displayState\":\"network-pairing\","
             "\"deckSupport\":{\"enabled\":%s,\"hostUsbSelected\":%s},"
             "\"lifecycle\":{\"pairingActive\":%s,\"paired\":%s,\"pairCode\":\"%04lu\"},"
             "\"eventCount\":%lu,"
             "\"latestAction\":\"%s\","
             "\"nextAction\":\"%s\"}",
             VOIDLINK_VERSION,
             VOIDLINK_URL,
             deck_support_enabled ? "true" : "false",
             deck_detected ? "true" : "false",
             pairing_active ? "true" : "false",
             paired ? "true" : "false",
             (unsigned long)pair_code,
             (unsigned long)event_count,
             latest_action,
             !deck_support_enabled ? "Press Enable T-Deck support."
                                    : (!deck_detected ? "Press Select T-Deck USB so the browser confirms the plugged-in deck."
                                                      : (paired ? "Pairing is saved on the dongle. Keep this page open for bridge status."
                                                                : (pairing_active ? "Confirm the matching pair code, then press Confirm here."
                                                                                  : "Press Begin Pair."))));

    return send_json(req, json);
}

static esp_err_t deck_support_on_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.deck_support_enabled = true;
    xSemaphoreGive(s_state_lock);

    state_update("deck-support-on");
    return status_get_handler(req);
}

static esp_err_t deck_support_off_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.deck_support_enabled = false;
    s_state.deck_detected = false;
    s_state.pairing_active = false;
    xSemaphoreGive(s_state_lock);

    state_update("deck-support-off");
    return status_get_handler(req);
}

static esp_err_t deck_detected_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.deck_support_enabled = true;
    s_state.deck_detected = true;
    xSemaphoreGive(s_state_lock);

    state_update("deck-usb-selected");
    return status_get_handler(req);
}

static esp_err_t pair_begin_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (s_state.deck_support_enabled) {
        s_state.pairing_active = true;
        s_state.paired = false;
        s_state.pair_code = 1000 + (esp_random() % 9000);
    }
    xSemaphoreGive(s_state_lock);

    state_update("pair-begin-request");
    return status_get_handler(req);
}

static esp_err_t pair_confirm_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (s_state.pairing_active) {
        s_state.paired = true;
        s_state.pairing_active = false;
    }
    xSemaphoreGive(s_state_lock);

    state_update("pair-confirm");
    return status_get_handler(req);
}

static esp_err_t pair_reset_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.pairing_active = false;
    s_state.paired = false;
    s_state.pair_code = 0;
    xSemaphoreGive(s_state_lock);

    state_update("pair-reset");
    return status_get_handler(req);
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting pairing server on port %d", config.server_port);
    ESP_ERROR_CHECK(httpd_start(&s_web_server, &config));

    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    const httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
    const httpd_uri_t deck_support_on = { .uri = "/api/deck-support-on", .method = HTTP_POST, .handler = deck_support_on_handler };
    const httpd_uri_t deck_support_off = { .uri = "/api/deck-support-off", .method = HTTP_POST, .handler = deck_support_off_handler };
    const httpd_uri_t deck_detected = { .uri = "/api/deck-detected", .method = HTTP_POST, .handler = deck_detected_handler };
    const httpd_uri_t pair_begin = { .uri = "/api/pair-begin", .method = HTTP_POST, .handler = pair_begin_handler };
    const httpd_uri_t pair_confirm = { .uri = "/api/pair-confirm", .method = HTTP_POST, .handler = pair_confirm_handler };
    const httpd_uri_t pair_reset = { .uri = "/api/pair-reset", .method = HTTP_POST, .handler = pair_reset_handler };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &deck_support_on));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &deck_support_off));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &deck_detected));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &pair_begin));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &pair_confirm));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &pair_reset));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_state_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_state_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    ESP_LOGI(TAG, "Starting VoidLink USB pairing appliance %s", VOIDLINK_VERSION);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    usb_ip_init_default_config();
    start_webserver();
    state_update("ready");

    ESP_LOGI(TAG, "VoidLink pairing UI ready at %s", VOIDLINK_URL);
}
