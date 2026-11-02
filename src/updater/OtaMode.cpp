#include "OtaMode.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <SDCardManager.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "DeviceLog.h"
#include "FirmwareSlots.h"

#ifndef INKLING_VERSION
#define INKLING_VERSION "dev"
#endif

namespace ota {

namespace {

constexpr unsigned long kConnectTimeoutMs = 20000;
constexpr unsigned long kIdleTimeoutMs = 15 * 60 * 1000;
constexpr size_t kChunk = 4096;

const Config* activeConfig = nullptr;

// Shared between the HTTP task and the UI loop.
std::atomic<int> progress{-1};
std::atomic<bool> restartPending{false};
std::atomic<bool> receiving{false};
std::atomic<unsigned long> lastActivityMs{0};
const char* volatile failure = nullptr;

// Leaves the updater: back to the reader in the other slot (a plain restart if
// that slot holds nothing bootable).
[[noreturn]] void leave() {
  firmware::bootOtherSlot();
  esp_restart();
}

void trimLine(char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) s[--n] = '\0';
}

bool tokenMatches(const char* given) {
  const char* expected = activeConfig->token;
  const size_t n = strlen(expected);
  if (n == 0 || strlen(given) != n) return false;
  uint8_t diff = 0;  // constant time over the token length
  for (size_t i = 0; i < n; ++i) diff |= static_cast<uint8_t>(given[i] ^ expected[i]);
  return diff == 0;
}

esp_err_t handleInfo(httpd_req_t* req) {
  lastActivityMs = millis();
  char body[256];
  const esp_partition_t* running = esp_ota_get_running_partition();
  snprintf(body, sizeof(body), "version=%s\nslot=%s\nother=%s\nfree_heap=%u\nmax_block=%u\n", INKLING_VERSION,
           running ? running->label : "?", firmware::otherSlotName(), static_cast<unsigned>(ESP.getFreeHeap()),
           static_cast<unsigned>(ESP.getMaxAllocHeap()));
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, body);
}

esp_err_t handleLog(httpd_req_t* req) {
  lastActivityMs = millis();
  // Heap, not .bss: update mode is the only user, and normal mode needs the RAM.
  constexpr size_t kLogCap = 3200;
  char* text = static_cast<char*>(malloc(kLogCap));
  if (!text) return httpd_resp_send_500(req);
  devlog::snapshot(text, kLogCap);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  const esp_err_t err = httpd_resp_sendstr(req, text);
  free(text);
  return err;
}

// Streams a log file the reader left on the SD card (its boot log survives
// the crash and the rollback that brought us here).
esp_err_t handleSdLog(httpd_req_t* req) {
  lastActivityMs = millis();
  char query[48] = {}, name[16] = "boot";
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    httpd_query_key_value(query, "name", name, sizeof(name));
  }
  const char* path = strcmp(name, "updater") == 0 ? "/.inkling/updater.log" : "/.inkling/boot.log";
  FsFile f = SdMan.open(path, O_RDONLY);
  if (!f) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "no such log on the card\n");
  }
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  char* buf = static_cast<char*>(malloc(1024));
  if (!buf) return httpd_resp_send_500(req);
  int n;
  while ((n = f.read(buf, 1024)) > 0) {
    if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) break;
  }
  free(buf);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t fail(httpd_req_t* req, const char* status, const char* why) {
  LOG("ota: rejected: %s", why);
  failure = why;
  receiving = false;
  progress = -1;
  httpd_resp_set_status(req, status);
  return httpd_resp_sendstr(req, why);
}

esp_err_t handleUpdate(httpd_req_t* req) {
  lastActivityMs = millis();
  char token[72] = {};
  if (httpd_req_get_hdr_value_str(req, "X-Inkling-Token", token, sizeof(token)) != ESP_OK || !tokenMatches(token)) {
    return fail(req, "403 Forbidden", "token mancante o errato");
  }
  const size_t total = req->content_len;
  if (total == 0) return fail(req, "400 Bad Request", "firmware vuoto");
  if (!Update.begin(total)) return fail(req, "413 Payload Too Large", "firmware troppo grande");

  LOG("ota: receiving %u bytes", static_cast<unsigned>(total));
  failure = nullptr;
  receiving = true;
  uint8_t* buf = static_cast<uint8_t*>(malloc(kChunk));  // update mode only: keep it off .bss
  if (!buf) {
    Update.abort();
    return fail(req, "500 Internal Server Error", "memoria esaurita");
  }
  struct FreeOnExit {
    uint8_t* p;
    ~FreeOnExit() { free(p); }
  } freeBuf{buf};
  size_t done = 0;
  bool first = true;
  while (done < total) {
    const int n = httpd_req_recv(req, reinterpret_cast<char*>(buf), kChunk);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      Update.abort();
      return fail(req, "500 Internal Server Error", "connessione interrotta");
    }
    if (first && buf[0] != 0xE9) {  // ESP image magic
      Update.abort();
      return fail(req, "400 Bad Request", "non e' un firmware ESP32");
    }
    first = false;
    if (Update.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      Update.abort();
      return fail(req, "500 Internal Server Error", "errore di scrittura in flash");
    }
    done += static_cast<size_t>(n);
    progress = static_cast<int>(done * 100 / total);
    lastActivityMs = millis();
  }
  // end() verifies checksum and SHA-256, then makes the new slot the boot slot.
  if (!Update.end(true)) return fail(req, "400 Bad Request", "firmware corrotto");

  LOG("ota: update ok (%u bytes), restarting", static_cast<unsigned>(total));
  receiving = false;
  httpd_resp_sendstr(req, "OK, riavvio\n");
  restartPending = true;
  return ESP_OK;
}

httpd_handle_t startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;  // Update.write runs flash operations on this task
  config.recv_wait_timeout = 15;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) return nullptr;
  static const httpd_uri_t routes[] = {
      {.uri = "/info", .method = HTTP_GET, .handler = handleInfo, .user_ctx = nullptr},
      {.uri = "/log", .method = HTTP_GET, .handler = handleLog, .user_ctx = nullptr},
      {.uri = "/sdlog", .method = HTTP_GET, .handler = handleSdLog, .user_ctx = nullptr},
      {.uri = "/update", .method = HTTP_POST, .handler = handleUpdate, .user_ctx = nullptr},
  };
  for (const auto& r : routes) httpd_register_uri_handler(server, &r);
  return server;
}

}  // namespace

bool loadConfig(Config& out) {
  out = Config{};
  if (!SdMan.ready()) return false;
  FsFile f = SdMan.open(kConfigPath, O_RDONLY);
  if (!f) return false;
  char line[160];
  while (f.available()) {
    const int n = f.fgets(line, sizeof(line));
    if (n <= 0) break;
    trimLine(line);
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    const char* key = line;
    const char* value = eq + 1;
    if (strcmp(key, "ssid") == 0) snprintf(out.ssid, sizeof(out.ssid), "%s", value);
    if (strcmp(key, "password") == 0) snprintf(out.password, sizeof(out.password), "%s", value);
    if (strcmp(key, "token") == 0) snprintf(out.token, sizeof(out.token), "%s", value);
  }
  return out.ssid[0] != '\0' && out.token[0] != '\0';
}

void run(const Config& config, const Hooks& hooks) {
  activeConfig = &config;
  inkling::OtaView view;
  view.network = config.ssid;
  view.version = INKLING_VERSION;
  view.state = "connessione...";
  snprintf(view.address, sizeof(view.address), "-");
  hooks.show(view, hooks.user);

  LOG("ota: connecting to '%s'", config.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("inkling");
  WiFi.begin(config.ssid, config.password);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kConnectTimeoutMs) {
    if (hooks.backPressed(hooks.user)) leave();
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG("ota: wifi connect failed (status %d)", static_cast<int>(WiFi.status()));
    view.state = "rete non raggiungibile";
    hooks.show(view, hooks.user);
    while (!hooks.backPressed(hooks.user)) delay(50);
    leave();
  }

  const String ip = WiFi.localIP().toString();
  MDNS.begin("inkling");
  MDNS.addService("http", "tcp", 80);
  snprintf(view.address, sizeof(view.address), "%s (inkling.local)", ip.c_str());
  LOG("ota: online at %s, free heap %u", ip.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));

  if (!startServer()) {
    view.state = "server non avviato";
    hooks.show(view, hooks.user);
    while (!hooks.backPressed(hooks.user)) delay(50);
    leave();
  }

  view.state = "in attesa";
  hooks.show(view, hooks.user);
  lastActivityMs = millis();

  int shownProgress = -1;
  const char* shownFailure = nullptr;
  for (;;) {
    if (restartPending) {
      view.state = "fatto, riavvio";
      view.progress = -1;
      hooks.show(view, hooks.user);
      delay(500);
      esp_restart();
    }
    // E-paper refreshes are slow: redraw progress in 20% steps only.
    const int p = progress;
    if (receiving && p / 20 != shownProgress / 20) {
      shownProgress = p;
      view.state = "ricezione";
      view.progress = static_cast<int8_t>(p);
      hooks.show(view, hooks.user);
    }
    const char* f = failure;
    if (f != shownFailure) {
      shownFailure = f;
      view.state = f ? f : "in attesa";
      view.progress = -1;
      shownProgress = -1;
      hooks.show(view, hooks.user);
    }
    if (hooks.backPressed(hooks.user)) leave();
    if (!receiving && millis() - lastActivityMs > kIdleTimeoutMs) {
      LOG("ota: idle timeout");
      leave();
    }
    delay(50);
  }
}

}  // namespace ota
