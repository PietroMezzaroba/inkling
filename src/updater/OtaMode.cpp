#include "OtaMode.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Rtc.h>
#include <SDCardManager.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_attr.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
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
constexpr const char* kLastBookPath = "/.inkling/last.txt";
constexpr const char* kBriefingPath = "/Daily Ink Briefing - Redazione Cyber-Net.md";

const Config* activeConfig = nullptr;
SemaphoreHandle_t spiMutex = nullptr;

struct SpiLock {
  SpiLock() {
    if (spiMutex) xSemaphoreTake(spiMutex, portMAX_DELAY);
  }
  ~SpiLock() {
    if (spiMutex) xSemaphoreGive(spiMutex);
  }
};

// Shared between the HTTP task and the UI loop.
std::atomic<int> progress{-1};
std::atomic<bool> restartPending{false};
std::atomic<bool> leaveToReaderPending{false};
std::atomic<bool> briefingPending{false};
std::atomic<bool> receiving{false};
std::atomic<unsigned long> lastActivityMs{0};
const char* volatile failure = nullptr;
char statusNotice[64] = "in attesa (OK: Daily Ink)";

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

bool endsWithNoCase(const char* s, const char* suffix) {
  const size_t n = strlen(s), m = strlen(suffix);
  if (n < m) return false;
  for (size_t i = 0; i < m; ++i) {
    char a = s[n - m + i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

bool isBookFileName(const char* name) {
  if (!name || name[0] == '.' || name[0] == '\0') return false;
  return endsWithNoCase(name, ".epub") || endsWithNoCase(name, ".txt") || endsWithNoCase(name, ".md");
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void urlDecode(const char* src, char* dst, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; src[i] && o + 1 < cap; ++i) {
    if (src[i] == '%' && src[i + 1] && src[i + 2]) {
      const int hi = hexVal(src[i + 1]);
      const int lo = hexVal(src[i + 2]);
      if (hi >= 0 && lo >= 0) {
        dst[o++] = static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    } else if (src[i] == '+') {
      dst[o++] = ' ';
      continue;
    }
    dst[o++] = src[i];
  }
  dst[o] = '\0';
}

void sanitizeFilename(const char* in, char* out, size_t cap) {
  size_t o = 0;
  if (cap < 4) return;
  out[o++] = '/';
  for (size_t i = 0; in[i] && o + 1 < cap; ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      continue;
    }
    if (c < 0x20) continue;
    out[o++] = static_cast<char>(c);
  }
  out[o] = '\0';
}

void setActiveBookOnSd(const char* path) {
  SdMan.mkdir("/.inkling");
  FsFile f = SdMan.open(kLastBookPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (f) {
    f.write(path, strlen(path));
    f.close();
  }
}

void syncClockAndRtc() {
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  struct tm ti {};
  if (getLocalTime(&ti, 2500) && (ti.tm_year + 1900) >= 2025) {
    freeink::Rtc rtc;
    if (rtc.begin()) {
      freeink::Rtc::DateTime dt;
      dt.year = static_cast<uint16_t>(ti.tm_year + 1900);
      dt.month = static_cast<uint8_t>(ti.tm_mon + 1);
      dt.day = static_cast<uint8_t>(ti.tm_mday);
      dt.hour = static_cast<uint8_t>(ti.tm_hour);
      dt.minute = static_cast<uint8_t>(ti.tm_min);
      dt.second = static_cast<uint8_t>(ti.tm_sec);
      dt.weekday = static_cast<uint8_t>(ti.tm_wday);
      rtc.set(dt);
      LOG("ota: NTP synced RTC to %04u-%02u-%02u %02u:%02u", dt.year, dt.month, dt.day, dt.hour, dt.minute);
    }
  }
}

// ---------------------------------------------------------------------------
// RSS / XML Text Sanitizer for Daily Ink Briefing
// ---------------------------------------------------------------------------
uint32_t writeUtf8Cp(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  return 0;
}

void cleanXmlText(const char* src, size_t srcLen, char* dst, size_t dstCap) {
  if (dstCap == 0) return;
  // Strip outer <![CDATA[ ... ]]> if present
  while (srcLen > 0 && (*src == ' ' || *src == '\n' || *src == '\r' || *src == '\t')) {
    ++src;
    --srcLen;
  }
  if (srcLen >= 12 && strncmp(src, "<![CDATA[", 9) == 0) {
    src += 9;
    srcLen -= 9;
    const char* endCdata = strstr(src, "]]>");
    if (endCdata && static_cast<size_t>(endCdata - src) <= srcLen) {
      srcLen = static_cast<size_t>(endCdata - src);
    }
  }

  size_t o = 0;
  bool inTag = false;
  bool lastSpace = true;
  for (size_t i = 0; i < srcLen && o + 4 < dstCap; ++i) {
    const char c = src[i];
    if (c == '<') {
      // Check again for inner <![CDATA[
      if (i + 9 <= srcLen && strncmp(src + i, "<![CDATA[", 9) == 0) {
        i += 8;
        continue;
      }
      inTag = true;
      if (!lastSpace && o > 0) {
        dst[o++] = ' ';
        lastSpace = true;
      }
      continue;
    }
    if (c == '>') {
      inTag = false;
      continue;
    }
    if (inTag) continue;
    if (i + 3 <= srcLen && strncmp(src + i, "]]>", 3) == 0) {
      i += 2;
      continue;
    }

    if (c == '&') {
      const char* semi = static_cast<const char*>(memchr(src + i, ';', srcLen - i < 12 ? srcLen - i : 12));
      if (semi) {
        const size_t elen = static_cast<size_t>(semi - (src + i + 1));
        const char* ent = src + i + 1;
        uint32_t cp = 0;
        if (elen == 3 && strncmp(ent, "amp", 3) == 0) cp = '&';
        else if (elen == 4 && strncmp(ent, "quot", 4) == 0) cp = '"';
        else if (elen == 4 && strncmp(ent, "apos", 4) == 0) cp = '\'';
        else if (elen == 2 && strncmp(ent, "lt", 2) == 0) cp = '<';
        else if (elen == 2 && strncmp(ent, "gt", 2) == 0) cp = '>';
        else if (elen == 4 && strncmp(ent, "nbsp", 4) == 0) cp = ' ';
        else if (elen == 6 && strncmp(ent, "egrave", 6) == 0) cp = 0xE8;
        else if (elen == 6 && strncmp(ent, "eacute", 6) == 0) cp = 0xE9;
        else if (elen == 6 && strncmp(ent, "agrave", 6) == 0) cp = 0xE0;
        else if (elen == 6 && strncmp(ent, "ograve", 6) == 0) cp = 0xF2;
        else if (elen == 6 && strncmp(ent, "ugrave", 6) == 0) cp = 0xF9;
        else if (elen == 6 && strncmp(ent, "igrave", 6) == 0) cp = 0xEC;
        else if (elen == 5 && strncmp(ent, "rsquo", 5) == 0) cp = '\'';
        else if (elen == 5 && strncmp(ent, "lsquo", 5) == 0) cp = '\'';
        else if (elen == 5 && strncmp(ent, "ldquo", 5) == 0) cp = '"';
        else if (elen == 5 && strncmp(ent, "rdquo", 5) == 0) cp = '"';
        else if (elen == 5 && strncmp(ent, "ndash", 5) == 0) cp = '-';
        else if (elen == 5 && strncmp(ent, "mdash", 5) == 0) cp = '-';
        else if (elen >= 2 && ent[0] == '#') {
          if (ent[1] == 'x' || ent[1] == 'X') {
            cp = static_cast<uint32_t>(strtoul(ent + 2, nullptr, 16));
          } else {
            cp = static_cast<uint32_t>(strtoul(ent + 1, nullptr, 10));
          }
        }
        if (cp > 0) {
          o += writeUtf8Cp(cp, dst + o);
          lastSpace = (cp == ' ');
          i = static_cast<size_t>(semi - src);
          continue;
        }
      }
    }

    if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
      if (!lastSpace && o > 0) {
        dst[o++] = ' ';
        lastSpace = true;
      }
      continue;
    }
    dst[o++] = c;
    lastSpace = false;
  }
  while (o > 0 && dst[o - 1] == ' ') --o;
  dst[o] = '\0';
}

bool extractTagSpan(const char* start, const char* limit, const char* openTag, const char* closeTag,
                    const char** outStart, size_t* outLen) {
  const size_t openLen = strlen(openTag);
  const char* p = strstr(start, openTag);
  if (!p || p >= limit) return false;
  p += openLen;
  const char* q = strstr(p, closeTag);
  if (!q || q > limit) return false;
  *outStart = p;
  *outLen = static_cast<size_t>(q - p);
  return true;
}

int fetchAndWriteRssSection(const char* sectionTitle, const char* url, int maxItems, char* xmlBuf, size_t xmlCap,
                            char* outMdBuf, size_t outMdCap) {
  size_t mdLen = 0;
  auto appendMd = [&](const char* s) {
    const size_t n = strlen(s);
    if (mdLen + n + 1 < outMdCap) {
      memcpy(outMdBuf + mdLen, s, n);
      mdLen += n;
      outMdBuf[mdLen] = '\0';
    }
  };

  char hdr[160];
  snprintf(hdr, sizeof(hdr), "\n# %s\n\n", sectionTitle);
  outMdBuf[0] = '\0';
  appendMd(hdr);

  size_t totalRead = 0;
  int httpCode = 0;
  {
    // Scope WiFiClientSecure & HTTPClient so all ~40 KB of mbedTLS buffers are
    // completely freed as soon as the HTTP response is read.
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(4);
    HTTPClient http;
    http.setConnectTimeout(3500);
    http.setTimeout(4000);
    http.useHTTP10(true);  // Avoid chunked encoding & keep-alive stalls!
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("InklingOS-XteinkX3/2.0");

    if (!http.begin(client, url)) {
      appendMd("Impossibile inizializzare connessione HTTPS.\n\n");
      goto write_sd;
    }
    httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      LOG("briefing: GET %s -> %d", url, httpCode);
      http.end();
      appendMd("Feed temporaneamente non raggiungibile.\n\n");
      goto write_sd;
    }

    WiFiClient* stream = http.getStreamPtr();
    const unsigned long t0 = millis();
    while (stream && (stream->connected() || stream->available()) && totalRead + 1 < xmlCap &&
           millis() - t0 < 4500) {
      const int avail = stream->available();
      if (avail > 0) {
        const size_t rem = xmlCap - 1 - totalRead;
        const size_t toRead = static_cast<size_t>(avail) < rem ? static_cast<size_t>(avail) : rem;
        const int n = stream->readBytes(xmlBuf + totalRead, toRead);
        if (n > 0) totalRead += static_cast<size_t>(n);
      } else {
        delay(5);
      }
    }
    xmlBuf[totalRead] = '\0';
    http.end();
  }

  {
    char titleBuf[280];
    char descBuf[520];
    char lineBuf[900];
    int count = 0;
    const char* cursor = xmlBuf;
    const char* xmlEnd = xmlBuf + totalRead;

    while (count < maxItems && cursor < xmlEnd) {
      const char* itemStart = strstr(cursor, "<item");
      if (!itemStart || itemStart >= xmlEnd) break;
      const char* itemEnd = strstr(itemStart, "</item>");
      if (!itemEnd || itemEnd > xmlEnd) break;

      const char* rawT = nullptr;
      size_t rawTLen = 0;
      if (extractTagSpan(itemStart, itemEnd, "<title>", "</title>", &rawT, &rawTLen)) {
        cleanXmlText(rawT, rawTLen, titleBuf, sizeof(titleBuf));
        if (titleBuf[0] != '\0') {
          ++count;
          descBuf[0] = '\0';
          const char* rawD = nullptr;
          size_t rawDLen = 0;
          if (extractTagSpan(itemStart, itemEnd, "<description>", "</description>", &rawD, &rawDLen)) {
            cleanXmlText(rawD, rawDLen, descBuf, sizeof(descBuf));
          }
          if (descBuf[0] != '\0' && strcmp(descBuf, titleBuf) != 0) {
            snprintf(lineBuf, sizeof(lineBuf), "## %d. %s\n\n%s\n\n", count, titleBuf, descBuf);
          } else {
            snprintf(lineBuf, sizeof(lineBuf), "## %d. %s\n\n", count, titleBuf);
          }
          appendMd(lineBuf);
        }
      }
      cursor = itemEnd + 7;
    }
    LOG("briefing: %s -> %d items (%u bytes XML)", sectionTitle, count, static_cast<unsigned>(totalRead));
    {
      SpiLock lock;
      FsFile outFile = SdMan.open(kBriefingPath, O_WRONLY | O_CREAT | O_APPEND);
      if (outFile) {
        outFile.write(outMdBuf, mdLen);
        outFile.close();
      }
    }
    return count;
  }

write_sd:
  {
    SpiLock lock;
    FsFile outFile = SdMan.open(kBriefingPath, O_WRONLY | O_CREAT | O_APPEND);
    if (outFile) {
      outFile.write(outMdBuf, mdLen);
      outFile.close();
    }
  }
  return 0;
}

bool generateDailyBriefing(inkling::OtaView& view, const Hooks& hooks) {
  // Keep buffers compact (28 KB XML + 8 KB Markdown) so WiFiClientSecure always
  // has >55 KB of contiguous heap for mbedTLS handshakes.
  constexpr size_t kXmlCap = 28672;
  constexpr size_t kMdCap = 8192;
  char* xmlBuf = static_cast<char*>(malloc(kXmlCap));
  char* mdBuf = static_cast<char*>(malloc(kMdCap));
  if (!xmlBuf || !mdBuf) {
    if (xmlBuf) free(xmlBuf);
    if (mdBuf) free(mdBuf);
    snprintf(statusNotice, sizeof(statusNotice), "Memoria insufficiente per Daily Ink");
    return false;
  }

  auto showStep = [&](const char* msg, int8_t pct) {
    SpiLock lock;
    view.state = msg;
    view.progress = pct;
    hooks.show(view, hooks.user);
  };

  showStep("1/3 Scarico ANSA Prima Pagina...", 20);

  {
    SpiLock lock;
    FsFile f = SdMan.open(kBriefingPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
      free(xmlBuf);
      free(mdBuf);
      snprintf(statusNotice, sizeof(statusNotice), "Errore scrittura su scheda SD");
      return false;
    }
    struct tm ti {};
    char dateLine[180];
    if (getLocalTime(&ti, 200) && (ti.tm_year + 1900) >= 2025) {
      snprintf(dateLine, sizeof(dateLine),
               "# DAILY INK BRIEFING\n\n> Edizione E-Ink del %02d/%02d/%04d ore %02d:%02d — Scaricata via Wi-Fi su Xteink X3.\n\n",
               ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900, ti.tm_hour, ti.tm_min);
    } else {
      snprintf(dateLine, sizeof(dateLine),
               "# DAILY INK BRIEFING\n\n> Edizione E-Ink Quotidiana — Scaricata via Wi-Fi su Xteink X3.\n\n");
    }
    f.write(dateLine, strlen(dateLine));
    f.close();
  }

  int totalArticles = 0;
  totalArticles += fetchAndWriteRssSection(
      "PRIMA PAGINA ITALIA (ANSA)", "https://www.ansa.it/sito/notizie/topnews/topnews_rss.xml", 10, xmlBuf, kXmlCap,
      mdBuf, kMdCap);

  showStep("2/3 Scarico ANSA Mondo & Esteri...", 55);
  totalArticles += fetchAndWriteRssSection(
      "MONDO & ESTERI (ANSA)", "https://www.ansa.it/sito/notizie/mondo/mondo_rss.xml", 8, xmlBuf, kXmlCap, mdBuf,
      kMdCap);

  showStep("3/3 Scarico ANSA Tecnologia...", 85);
  totalArticles += fetchAndWriteRssSection(
      "TECNOLOGIA & SCIENZA (ANSA)", "https://www.ansa.it/sito/notizie/tecnologia/tecnologia_rss.xml", 8, xmlBuf,
      kXmlCap, mdBuf, kMdCap);

  free(xmlBuf);
  free(mdBuf);

  {
    SpiLock lock;
    FsFile f = SdMan.open(kBriefingPath, O_WRONLY | O_CREAT | O_APPEND);
    if (f) {
      const char* footer =
          "\n> Fine edizione Daily Ink Briefing. Premi BACK per tornare al Lettore e leggere il giornale!\n";
      f.write(footer, strlen(footer));
      f.close();
    }
    setActiveBookOnSd(kBriefingPath);
    snprintf(statusNotice, sizeof(statusNotice), "Giornale pronto! (%d notizie) Premi BACK", totalArticles);
    view.state = statusNotice;
    view.progress = 100;
    hooks.show(view, hooks.user);
  }
  return totalArticles > 0;
}

// ---------------------------------------------------------------------------
// Cyber-Deck Interactive Web Portal (HTML5 + CSS + JS)
// ---------------------------------------------------------------------------
static const char kCyberDeckHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>INKLING OS // CYBER-DECK X3</title>
<style>
:root{--bg:#0b0e14;--card:#131924;--fg:#e6edf3;--mut:#8b949e;--acc:#00ff9d;--bdr:#263248;--danger:#ff5572}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--fg);font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;padding:16px;max-width:780px;margin:0 auto;line-height:1.45}
header{border:2px solid var(--acc);background:var(--card);padding:16px;border-radius:12px;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;box-shadow:0 0 24px rgba(0,255,157,.12)}
h1{font-size:1.15rem;color:var(--acc);letter-spacing:1px}
.sub{font-size:.8rem;color:var(--mut)}
.grid{display:grid;grid-template-columns:1fr;gap:16px}
@media(min-width:640px){.grid{grid-template-columns:1fr 1fr}}
.card{background:var(--card);border:1px solid var(--bdr);border-radius:12px;padding:16px;display:flex;flex-direction:column;gap:10px}
.card.full{grid-column:1 / -1}
h2{font-size:.95rem;color:var(--acc);text-transform:uppercase;letter-spacing:.8px;border-bottom:1px solid var(--bdr);padding-bottom:6px}
p{font-size:.82rem;color:var(--mut)}
input,textarea{width:100%;background:#080b10;border:1px solid var(--bdr);color:var(--fg);padding:10px;border-radius:8px;font-family:inherit;font-size:.85rem}
textarea{min-height:120px;resize:vertical}
button,.btn{background:var(--acc);color:#05080c;border:none;padding:11px 14px;border-radius:8px;font-weight:700;font-family:inherit;font-size:.85rem;cursor:pointer;text-align:center;transition:opacity .15s}
button:hover{opacity:.88}
button.sec{background:transparent;color:var(--acc);border:1px solid var(--acc)}
button.del{background:transparent;color:var(--danger);border:1px solid var(--danger);padding:5px 10px;font-size:.75rem}
.book-row{display:flex;justify-content:space-between;align-items:center;padding:8px 10px;background:#080b10;border:1px solid var(--bdr);border-radius:8px;gap:8px}
.book-name{font-size:.82rem;word-break:break-all;flex:1}
.book-sz{font-size:.72rem;color:var(--mut);white-space:nowrap}
.toast{padding:10px 12px;border-radius:8px;background:#09261c;border:1px solid var(--acc);color:var(--acc);font-size:.82rem;display:none;margin-bottom:14px}
progress{width:100%;height:12px;accent-color:var(--acc);display:none}
</style>
</head>
<body>
<header>
  <div>
    <h1>INKLING OS // CYBER-DECK X3</h1>
    <div class="sub">Terminale Wireless E-Ink · Daily Ink · Second Brain · Libreria SD</div>
  </div>
  <button class="sec" onclick="rebootReader()">TORNA AL LETTORE X3</button>
</header>
<div id="toast" class="toast"></div>
<div class="grid">
  <div class="card">
    <h2>1. Daily Ink Briefing</h2>
    <p>Scarica ora le ultime notizie da <b>ANSA Prima Pagina</b>, <b>ANSA Tecnologia</b> e <b>Hacker News</b> e crea un volume E-Ink pronto per la lettura Bionic.</p>
    <button onclick="runBriefing()">SCARICA DAILY INK ORA</button>
    <button class="sec" onclick="rebootReader()">APRI SUBITO SU XTEINK X3</button>
  </div>

  <div class="card">
    <h2>2. Carica Libro (.EPUB / .TXT / .MD)</h2>
    <p>Invia un libro dal tuo telefono o Mac direttamente sulla microSD dell'Xteink X3 senza staccare la scheda.</p>
    <input type="file" id="bookFile" accept=".epub,.txt,.md">
    <progress id="upProg" value="0" max="100"></progress>
    <button onclick="uploadFile()">CARICA LIBRO SU MICROSD</button>
  </div>

  <div class="card full">
    <h2>3. Send to Xteink // Second Brain & Articoli</h2>
    <p>Incolla un articolo web, appunti dal tuo Second Brain o una nota Markdown: verrà impaginato come libro con titoli e paragrafi sull'Xteink X3.</p>
    <div style="display:flex;gap:10px;flex-wrap:wrap">
      <input id="noteTitle" placeholder="Titolo (es. Note Architettura AI)" style="flex:2;min-width:180px">
      <input id="noteAuthor" placeholder="Autore" value="Second Brain" style="flex:1;min-width:130px">
    </div>
    <textarea id="noteBody" placeholder="# Capitolo 1&#10;&#10;Incolla qui l'articolo o i tuoi appunti..."></textarea>
    <button onclick="sendNote()">INVIA TESTO COME LIBRO A XTEINK X3</button>
  </div>

  <div class="card full">
    <h2>4. Libreria MicroSD (<span id="bookCount">0</span> volumi)</h2>
    <div id="bookList" style="display:flex;flex-direction:column;gap:8px">Caricamento...</div>
  </div>
</div>

<script>
function msg(t){const el=document.getElementById('toast');el.textContent=t;el.style.display='block';}
async function runBriefing(){
  msg('Scaricamento ANSA + Tech + Hacker News in corso su Xteink X3 (guarda lo schermo E-Ink)...');
  const r=await fetch('/briefing',{method:'POST'});
  msg(await r.text());
  setTimeout(loadBooks,6000);
}
async function rebootReader(){
  msg('Riavvio nel Lettore Xteink X3 in corso...');
  await fetch('/reboot',{method:'POST'});
}
function uploadRaw(name,blob){
  return new Promise((res,rej)=>{
    const p=document.getElementById('upProg');
    p.style.display='block';p.value=0;
    const xhr=new XMLHttpRequest();
    xhr.open('POST','/upload?name='+encodeURIComponent(name));
    xhr.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total);};
    xhr.onload=()=>{p.style.display='none';res(xhr.responseText);};
    xhr.onerror=()=>{p.style.display='none';rej('Errore rete');};
    xhr.send(blob);
  });
}
async function uploadFile(){
  const f=document.getElementById('bookFile').files[0];
  if(!f){msg('Seleziona prima un file .epub, .txt o .md');return;}
  msg('Invio di '+f.name+' in corso...');
  const txt=await uploadRaw(f.name,f);
  msg(txt);
  loadBooks();
}
async function sendNote(){
  const title=(document.getElementById('noteTitle').value||'Nota Rapida').trim();
  const author=(document.getElementById('noteAuthor').value||'Second Brain').trim();
  const body=document.getElementById('noteBody').value.trim();
  if(!body){msg('Inserisci il testo della nota o articolo');return;}
  const fname=title+' - '+author+'.md';
  const md='# '+title+'\n\n> Autore: '+author+'\n\n'+body+'\n';
  msg('Salvataggio di "'+fname+'" su Xteink X3...');
  const txt=await uploadRaw(fname,new Blob([md],{type:'text/markdown;charset=utf-8'}));
  msg(txt);
  document.getElementById('noteBody').value='';
  loadBooks();
}
async function openOnX3(name){
  msg('Apertura di "'+name+'" sul Lettore X3...');
  await fetch('/activate?name='+encodeURIComponent(name),{method:'POST'});
}
async function delBook(name){
  if(!confirm('Eliminare '+name+' dalla microSD?'))return;
  await fetch('/delete?name='+encodeURIComponent(name),{method:'POST'});
  loadBooks();
}
async function loadBooks(){
  try{
    const r=await fetch('/books');
    const list=await r.json();
    document.getElementById('bookCount').textContent=list.length;
    const box=document.getElementById('bookList');
    box.innerHTML='';
    list.forEach(b=>{
      const row=document.createElement('div');
      row.className='book-row';
      const kb=Math.max(1,Math.round(b.size/1024));
      row.innerHTML='<span class="book-name"></span><span class="book-sz">'+kb+' KB</span>'+
        '<button class="sec" style="padding:5px 10px;font-size:.75rem">LEGGI SU X3</button>'+
        '<button class="del">X</button>';
      row.querySelector('.book-name').textContent=b.name;
      row.querySelectorAll('button')[0].onclick=()=>openOnX3(b.name);
      row.querySelectorAll('button')[1].onclick=()=>delBook(b.name);
      box.appendChild(row);
    });
  }catch(e){}
}
loadBooks();
</script>
</body>
</html>)rawliteral";

// ---------------------------------------------------------------------------
// HTTP Handlers
// ---------------------------------------------------------------------------
esp_err_t handleRoot(httpd_req_t* req) {
  lastActivityMs = millis();
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, kCyberDeckHtml, strlen(kCyberDeckHtml));
}

esp_err_t handleBooks(httpd_req_t* req) {
  lastActivityMs = millis();
  SpiLock lock;
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_sendstr_chunk(req, "[");
  FsFile root = SdMan.open("/");
  bool first = true;
  if (root && root.isDirectory()) {
    char name[128];
    char entryJson[220];
    while (FsFile entry = root.openNextFile()) {
      if (!entry.isDirectory()) {
        entry.getName(name, sizeof(name));
        if (isBookFileName(name)) {
          // Escape quotes if any
          for (char* p = name; *p; ++p) {
            if (*p == '"' || *p == '\\') *p = '_';
          }
          snprintf(entryJson, sizeof(entryJson), "%s{\"name\":\"%s\",\"size\":%lu}", first ? "" : ",", name,
                   static_cast<unsigned long>(entry.fileSize()));
          httpd_resp_sendstr_chunk(req, entryJson);
          first = false;
        }
      }
      entry.close();
    }
    root.close();
  }
  httpd_resp_sendstr_chunk(req, "]");
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handleUpload(httpd_req_t* req) {
  lastActivityMs = millis();
  char query[200] = {}, rawName[128] = {}, decodedName[128] = {}, path[136] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "name", rawName, sizeof(rawName)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "Nome file mancante");
  }
  urlDecode(rawName, decodedName, sizeof(decodedName));
  sanitizeFilename(decodedName, path, sizeof(path));
  if (!isBookFileName(path + 1)) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "Formato supportato: .epub, .txt, .md");
  }

  const size_t total = req->content_len;
  if (total == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "File vuoto");
  }

  uint8_t* buf = static_cast<uint8_t*>(malloc(kChunk));
  if (!buf) return httpd_resp_send_500(req);

  SpiLock lock;
  FsFile f = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) {
    free(buf);
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_sendstr(req, "Impossibile scrivere sulla microSD");
  }

  size_t done = 0;
  while (done < total) {
    const int n = httpd_req_recv(req, reinterpret_cast<char*>(buf), kChunk);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      f.close();
      SdMan.remove(path);
      free(buf);
      httpd_resp_set_status(req, "500 Internal Server Error");
      return httpd_resp_sendstr(req, "Trasferimento interrotto");
    }
    f.write(buf, static_cast<size_t>(n));
    done += static_cast<size_t>(n);
    lastActivityMs = millis();
  }
  f.close();
  free(buf);

  setActiveBookOnSd(path);
  snprintf(statusNotice, sizeof(statusNotice), "Salvato: %s", path + 1);
  LOG("ota: uploaded book %s (%u bytes)", path, static_cast<unsigned>(total));

  char resp[180];
  snprintf(resp, sizeof(resp), "Salvato su Xteink X3: %s (%u KB)", path + 1,
           static_cast<unsigned>((total + 1023) / 1024));
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(req, resp);
}

esp_err_t handleDelete(httpd_req_t* req) {
  lastActivityMs = millis();
  char query[200] = {}, rawName[128] = {}, decodedName[128] = {}, path[136] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "name", rawName, sizeof(rawName)) == ESP_OK) {
    urlDecode(rawName, decodedName, sizeof(decodedName));
    sanitizeFilename(decodedName, path, sizeof(path));
    if (isBookFileName(path + 1)) {
      SpiLock lock;
      SdMan.remove(path);
      snprintf(statusNotice, sizeof(statusNotice), "Eliminato: %s", path + 1);
    }
  }
  return httpd_resp_sendstr(req, "OK");
}

esp_err_t handleActivate(httpd_req_t* req) {
  lastActivityMs = millis();
  char query[200] = {}, rawName[128] = {}, decodedName[128] = {}, path[136] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "name", rawName, sizeof(rawName)) == ESP_OK) {
    urlDecode(rawName, decodedName, sizeof(decodedName));
    sanitizeFilename(decodedName, path, sizeof(path));
    if (isBookFileName(path + 1)) {
      SpiLock lock;
      setActiveBookOnSd(path);
    }
  }
  httpd_resp_sendstr(req, "OK");
  leaveToReaderPending = true;
  return ESP_OK;
}

esp_err_t handleBriefing(httpd_req_t* req) {
  lastActivityMs = millis();
  briefingPending = true;
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(req, "Download Daily Ink Briefing avviato su Xteink X3! Guarda lo schermo E-Ink.");
}

esp_err_t handleRebootReader(httpd_req_t* req) {
  lastActivityMs = millis();
  httpd_resp_sendstr(req, "Riavvio nel Lettore X3...");
  leaveToReaderPending = true;
  return ESP_OK;
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
  constexpr size_t kLogCap = 3200;
  char* text = static_cast<char*>(malloc(kLogCap));
  if (!text) return httpd_resp_send_500(req);
  devlog::snapshot(text, kLogCap);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  const esp_err_t err = httpd_resp_sendstr(req, text);
  free(text);
  return err;
}

esp_err_t handleSdLog(httpd_req_t* req) {
  lastActivityMs = millis();
  SpiLock lock;
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

esp_err_t handleCoredump(httpd_req_t* req) {
  lastActivityMs = millis();
  const esp_partition_t* p =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!p) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "no coredump partition\n");
  }
  uint8_t* buf = static_cast<uint8_t*>(malloc(2048));
  if (!buf) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "application/octet-stream");
  for (uint32_t off = 0; off < p->size; off += 2048) {
    if (esp_partition_read(p, off, buf, 2048) != ESP_OK) break;
    if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(buf), 2048) != ESP_OK) break;
  }
  free(buf);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handleFile(httpd_req_t* req) {
  lastActivityMs = millis();
  SpiLock lock;
  char query[160] = {}, path[128] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK || path[0] != '/') {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "use /file?path=/absolute/path\n");
  }
  FsFile f = SdMan.open(path, O_RDONLY);
  if (!f || f.isDirectory()) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "no such file\n");
  }
  char* buf = static_cast<char*>(malloc(2048));
  if (!buf) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "application/octet-stream");
  int n;
  while ((n = f.read(buf, 2048)) > 0) {
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
  uint8_t* buf = static_cast<uint8_t*>(malloc(kChunk));
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
    if (first && buf[0] != 0xE9) {
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
  if (!Update.end(true)) return fail(req, "400 Bad Request", "firmware corrotto");

  LOG("ota: update ok (%u bytes), restarting", static_cast<unsigned>(total));
  receiving = false;
  httpd_resp_sendstr(req, "OK, riavvio\n");
  restartPending = true;
  return ESP_OK;
}

httpd_handle_t startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 14;
  config.recv_wait_timeout = 15;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) return nullptr;
  static const httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = handleRoot, .user_ctx = nullptr},
      {.uri = "/books", .method = HTTP_GET, .handler = handleBooks, .user_ctx = nullptr},
      {.uri = "/upload", .method = HTTP_POST, .handler = handleUpload, .user_ctx = nullptr},
      {.uri = "/delete", .method = HTTP_POST, .handler = handleDelete, .user_ctx = nullptr},
      {.uri = "/activate", .method = HTTP_POST, .handler = handleActivate, .user_ctx = nullptr},
      {.uri = "/briefing", .method = HTTP_POST, .handler = handleBriefing, .user_ctx = nullptr},
      {.uri = "/reboot", .method = HTTP_POST, .handler = handleRebootReader, .user_ctx = nullptr},
      {.uri = "/info", .method = HTTP_GET, .handler = handleInfo, .user_ctx = nullptr},
      {.uri = "/log", .method = HTTP_GET, .handler = handleLog, .user_ctx = nullptr},
      {.uri = "/sdlog", .method = HTTP_GET, .handler = handleSdLog, .user_ctx = nullptr},
      {.uri = "/coredump", .method = HTTP_GET, .handler = handleCoredump, .user_ctx = nullptr},
      {.uri = "/file", .method = HTTP_GET, .handler = handleFile, .user_ctx = nullptr},
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
  if (!spiMutex) spiMutex = xSemaphoreCreateMutex();

  inkling::OtaView view;
  view.network = config.ssid;
  view.version = INKLING_VERSION;
  view.state = "connessione Wi-Fi...";
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
  snprintf(view.address, sizeof(view.address), "%s", ip.c_str());
  LOG("ota: online at %s, free heap %u", ip.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));

  syncClockAndRtc();

  if (!startServer()) {
    view.state = "server non avviato";
    hooks.show(view, hooks.user);
    while (!hooks.backPressed(hooks.user)) delay(50);
    leave();
  }

  view.state = statusNotice;
  hooks.show(view, hooks.user);
  lastActivityMs = millis();

  int shownProgress = -1;
  const char* shownFailure = nullptr;
  char lastShownNotice[64] = {};
  snprintf(lastShownNotice, sizeof(lastShownNotice), "%s", statusNotice);

  for (;;) {
    if (restartPending) {
      SpiLock lock;
      view.state = "fatto, riavvio";
      view.progress = -1;
      hooks.show(view, hooks.user);
      delay(500);
      esp_restart();
    }
    if (leaveToReaderPending) {
      SpiLock lock;
      view.state = "apertura Lettore X3...";
      view.progress = -1;
      hooks.show(view, hooks.user);
      delay(350);
      leave();
    }
    if (briefingPending || (hooks.confirmPressed && hooks.confirmPressed(hooks.user))) {
      briefingPending = false;
      lastActivityMs = millis();
      if (generateDailyBriefing(view, hooks)) {
        delay(600);
        leaveToReaderPending = true;
      }
      snprintf(lastShownNotice, sizeof(lastShownNotice), "%s", statusNotice);
    }

    const int p = progress;
    if (receiving && p / 20 != shownProgress / 20) {
      SpiLock lock;
      shownProgress = p;
      view.state = "ricezione firmware";
      view.progress = static_cast<int8_t>(p);
      hooks.show(view, hooks.user);
    }
    const char* f = failure;
    if (f != shownFailure) {
      SpiLock lock;
      shownFailure = f;
      view.state = f ? f : statusNotice;
      view.progress = -1;
      shownProgress = -1;
      hooks.show(view, hooks.user);
    } else if (!receiving && !f && strcmp(lastShownNotice, statusNotice) != 0) {
      SpiLock lock;
      snprintf(lastShownNotice, sizeof(lastShownNotice), "%s", statusNotice);
      view.state = statusNotice;
      view.progress = -1;
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
