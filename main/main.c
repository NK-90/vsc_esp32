//241002
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#define WIFI_SSID "iptime"
#define WIFI_PASS ""
#define MAX_RETRY 5

static const char *TAG = "Camera";

// AI Thinker 모델을 위한 카메라 설정
static camera_config_t camera_config = {
    .pin_pwdn = 32,
    .pin_reset = -1,
    .pin_xclk = 0,
    .pin_sscb_sda = 26,
    .pin_sscb_scl = 27,
    .pin_d7 = 35,
    .pin_d6 = 34,
    .pin_d5 = 39,
    .pin_d4 = 36,
    .pin_d3 = 21,
    .pin_d2 = 19,
    .pin_d1 = 18,
    .pin_d0 = 5,
    .pin_vsync = 25,
    .pin_href = 23,
    .pin_pclk = 22,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_UXGA,
    .jpeg_quality = 12,
    .fb_count = 1
};

// HTTP GET 핸들러: 카메라 이미지 스트리밍 (영상.ver)
esp_err_t stream_handler(httpd_req_t *req) {
    char *part_buf[64];
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        size_t hlen = snprintf((char *)part_buf, 64, "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);
        httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        httpd_resp_send_chunk(req, "\r\n--frame\r\n", 13);
        esp_camera_fb_return(fb);
    }
    return ESP_OK;
}

// 웹 서버 시작
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
    }
    return server;
}

// WiFi 이벤트 핸들러
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected. Attempting to reconnect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// WiFi 연결 함수
static void wifi_connect()
{
    int retry = 0;
    while (retry < MAX_RETRY) {
        esp_err_t result = esp_wifi_connect();
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected");
            return;
        }
        ESP_LOGI(TAG, "WiFi connect failed. Retrying...");
        retry++;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    ESP_LOGE(TAG, "WiFi connection failed after %d attempts", MAX_RETRY);
    esp_restart();
}

// WiFi 초기화
void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_connect();
}

void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();

    // 카메라 초기화
    ESP_ERROR_CHECK(esp_camera_init(&camera_config));

    // 웹 서버 시작
    start_webserver();
}