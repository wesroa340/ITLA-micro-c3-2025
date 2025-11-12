#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define WIFI_SSID_AP       "MICRO"
#define WIFI_PASS_AP       "12345678"
#define MQTT_BROKER_URI    "mqtt://broker.emqx.io"
#define MQTT_TOPIC         "test/esp32_ap"

static const char *TAG = "ESP32_AP_MQTT";
static esp_mqtt_client_handle_t client = NULL;

// --- Manejador de eventos MQTT ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al broker MQTT");
            esp_mqtt_client_subscribe(client, MQTT_TOPIC, 0);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Mensaje recibido:");
            printf("Tópico: %.*s\n", event->topic_len, event->topic);
            printf("Contenido: %.*s\n", event->data_len, event->data);
            break;

        default:
            break;
    }
}

// --- Inicializa el cliente MQTT ---
void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = MQTT_BROKER_URI,
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// --- Configura el ESP32 como Access Point ---
static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID_AP,
            .ssid_len = strlen(WIFI_SSID_AP),
            .password = WIFI_PASS_AP,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    if (strlen(WIFI_PASS_AP) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP iniciado:");
    ESP_LOGI(TAG, "  SSID: %s", WIFI_SSID_AP);
    ESP_LOGI(TAG, "  PASS: %s", WIFI_PASS_AP);
}

void app_main(void) {
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap();  // Crea la red propia del ESP32
    mqtt_app_start();    // Conecta al broker MQTT

    // Publicar mensajes cada 5 segundos
    while (1) {
        if (client) {
            esp_mqtt_client_publish(client, MQTT_TOPIC, "Mensaje desde ESP32 (modo AP)", 0, 0, 0);
            ESP_LOGI(TAG, "Mensaje publicado en %s", MQTT_TOPIC);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
