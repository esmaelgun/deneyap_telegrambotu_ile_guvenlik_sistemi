#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include <Deneyap_OLED.h>
#include <HTTPClient.h>

OLED OLED;

// ==== Wi-Fi ====
#define WIFI_SSID "*********"
#define WIFI_PASS "*********"

// ==== Telegram bilgileri ====
#define BOT_TOKEN "**********"
#define CHAT_ID   "**********"

// ==== Fonksiyon bildirimi ====
void cameraInit(void);
void startCameraServer();
void sendTelegram(const char *mesaj);

// ==== Kamera server ====
httpd_handle_t stream_httpd = NULL;

// ==== Önceki kare için değer ====
int prevValue = -1;

// ==== Mesaj kontrol ====
unsigned long sonMesajZamani = 0;
const unsigned long mesajAraligi = 10000; // 10 sn
bool telegramGonderFlag = false;

void setup() {
  Serial.begin(115200);

  // OLED başlat
  if (!OLED.begin(0x7A)) {
    delay(3000);
    Serial.println("I2C bağlantısı başarısız!");
  }
  OLED.clearDisplay();
  OLED.setTextXY(2, 0);
  OLED.putString("Kamera Basliyor..");

  // Kamera başlat
  cameraInit();

  // Wi-Fi başlat
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Wi-Fi Baglaniyor...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Baglandi!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Açılışta test mesajı
  sendTelegram("ESP32 acildi, kamera aktif!");

  // Kamera server başlat
  startCameraServer();

  Serial.print("Kamera hazir! Baglanmak icin: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Kare al
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Kare alinmadi!");
    return;
  }

  // Ortalama değer hesapla
  long sum = 0;
  int count = 0;
  for (int i = 0; i < fb->len; i += 200) {
    sum += fb->buf[i];
    count++;
  }
  int avgValue = sum / count;

  // Farkı kontrol et
  if (prevValue >= 0) {
    int diff = abs(avgValue - prevValue);

    OLED.clearDisplay();
    if (diff > 5) {  // eşik değeri
      OLED.setTextXY(2, 0);
      OLED.putString("Hareket Algilandi!");
      Serial.print("Hareket Algilandi! diff=");
      Serial.println(diff);

      if (millis() - sonMesajZamani > mesajAraligi) {
        telegramGonderFlag = true; // mesaj göndermeyi işaretle
        sonMesajZamani = millis();
      }

    } else {
      OLED.setTextXY(2, 0);
      OLED.putString("Guvenli Bolge");
      Serial.print("Guvenli Bolge diff=");
      Serial.println(diff);
    }
  }

  prevValue = avgValue;
  esp_camera_fb_return(fb);

  // Kamera işinden ayrı mesaj gönder
  if (telegramGonderFlag) {
    sendTelegram("Hareket algilandi!");
    telegramGonderFlag = false;
  }

  delay(300);
}

// ==== Telegram Mesaj Fonksiyonu ====
void sendTelegram(const char *mesaj) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    String url = "https://api.telegram.org/bot";
    url += BOT_TOKEN;
    url += "/sendMessage?chat_id=";
    url += CHAT_ID;
    url += "&text=";
    url += mesaj;   // sadece düz metin

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      Serial.println("✅ Telegram mesaj gonderildi");
    } else {
      Serial.printf("❌ Telegram hata kodu: %d\n", httpCode);
    }
    http.end();
  }
}

// ==== Kamera Baslatma ====
void cameraInit(void) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAMD2;
  config.pin_d1 = CAMD3;
  config.pin_d2 = CAMD4;
  config.pin_d3 = CAMD5;
  config.pin_d4 = CAMD6;
  config.pin_d5 = CAMD7;
  config.pin_d6 = CAMD8;
  config.pin_d7 = CAMD9;
  config.pin_xclk = CAMXC;
  config.pin_pclk = CAMPC;
  config.pin_vsync = CAMV;
  config.pin_href = CAMH;
  config.pin_sscb_sda = CAMSD;
  config.pin_sscb_scl = CAMSC;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 15000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Kamera baslatilamadi!");
    return;
  }
  Serial.println("Kamera baslatildi");
}

// ==== Kamera Sunucu ====
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t* fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];
  static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
  static const char* _STREAM_BOUNDARY = "\r\n--frame\r\n";
  static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Kamera goruntu alinamadi");
      return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    sprintf((char*)part_buf, _STREAM_PART, fb->len);
    httpd_resp_send_chunk(req, (const char*)part_buf, strlen((char*)part_buf));
    httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t stream_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}
