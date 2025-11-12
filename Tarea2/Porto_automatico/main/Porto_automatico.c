#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "mqtt_client.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_timer.h"

// ========================== CONFIGURACIÓN DE HARDWARE ==========================
// >>>>>>>>>>>> AJUSTA ESTOS PINS A TU PCB <<<<<<<<<<<<
#define GPIO_MA             19   // Salida puente H - A (abrir)
#define GPIO_MC             18   // Salida puente H - C (cerrar)
#define GPIO_LAMP           13    // LED / Lámpara estado
#define GPIO_EMERG_OUT      27   // Pin a '1' cuando hay EMERGENCIA


// Botones físicos (entrada con pull-up, pulsador a GND)
#define GPIO_BTN_PP         32
#define GPIO_BTN_ABRIR      33
#define GPIO_BTN_CERRAR     34  // OJO: 34-39 no tienen pullups internos
#define GPIO_BTN_EMERG      35  // idem
#define GPIO_TRIG_ABRIR     14   // Entrada: 0 => disparar ABRIENDO

// Limit switches (entrada con pull-up, contacto a GND)
#define GPIO_LSA            4   // Fin de carrera ABIERTO (0 = activo)
#define GPIO_LSC            5   // Fin de carrera CERRADO (0 = activo)

// ============================ Wi-Fi (sin ejemplos) ============================
#define WIFI_SSID           "Docentes_Administrativos"
#define WIFI_PASS           "Adm1N2584km"
#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t wifi_event_group;

// ============================ MQTT (brokers y tópicos) ========================
#define MQTT_BROKER_URI     "ws://broker.emqx.io:8083/mqtt"
#define MQTT_USER           "easy-learning"
#define MQTT_PASS           "demo-para-el-canal"

#define MQTT_TOPIC_CMD      "casa/porton/cmd"
#define MQTT_TOPIC_STATUS   "casa/porton/status"
#define MQTT_TOPIC_TELE     "casa/porton/tele"

// ============================== ESTADOS / LÁMPARA =============================
typedef enum {
    EST_INIT = 0,
    EST_CERRADO,
    EST_ABIERTO,
    EST_ABRIENDO,
    EST_CERRANDO,
    EST_PARADO,
    EST_EMERGENCIA,
    EST_ERROR
} estado_t;

typedef enum {
    LAMP_OFF = 0,
    LAMP_ABRIENDO,   // 1 Hz
    LAMP_CERRANDO,   // 2 Hz
    LAMP_EMERG       // 4 Hz
} lamp_modo_t;

// ============================== EVENTOS DEL SISTEMA ============================
typedef enum {
    EV_NONE = 0,
    EV_BTN_PP,
    EV_BTN_ABRIR,
    EV_BTN_CERRAR,
    EV_BTN_EMERG,
    EV_MQTT_PP,
    EV_MQTT_ABRIR,
    EV_MQTT_CERRAR,
    EV_MQTT_EMERG,
    EV_MQTT_RESET,
    EV_LSA_ACTIVO,
    EV_LSC_ACTIVO,
    EV_TICK_50MS
} evento_t;

typedef struct { evento_t type; } app_event_t;

// ============================== GLOBALES =======================================
static const char *TAG = "PORTON_MQTT";
static esp_mqtt_client_handle_t mqtt_client = NULL;

static estado_t g_estado = EST_INIT;
static estado_t g_prev_dir = EST_CERRANDO; // para PP

static lamp_modo_t g_lamp = LAMP_OFF;

static QueueHandle_t g_evt_queue = NULL;
static TimerHandle_t g_timer50 = NULL;

// Antirrebote ISR
static volatile int64_t isr_last_us_pp      = 0;
static volatile int64_t isr_last_us_abrir   = 0;
static volatile int64_t isr_last_us_cerrar  = 0;
static volatile int64_t isr_last_us_emerg   = 0;
#define DEBOUNCE_US 200000  // 200 ms

// ============================== UTILIDADES GPIO ================================
static inline int LSA_is_active(void) { return gpio_get_level(GPIO_LSA) == 0; } // activo a 0
static inline int LSC_is_active(void) { return gpio_get_level(GPIO_LSC) == 0; }

static void motor_parar(void) {
    gpio_set_level(GPIO_MA, 0);
    gpio_set_level(GPIO_MC, 0);
}

static void motor_abrir(void) {
    if (!LSA_is_active()) { // no abras si ya está abierto
        gpio_set_level(GPIO_MC, 0);
        gpio_set_level(GPIO_MA, 1);
    } else {
        motor_parar();
    }
}

static void motor_cerrar(void) {
    if (!LSC_is_active()) { // no cierres si ya está cerrado
        gpio_set_level(GPIO_MA, 0);
        gpio_set_level(GPIO_MC, 1);
    } else {
        motor_parar();
    }
}

static void set_emerg_pin(bool on) { gpio_set_level(GPIO_EMERG_OUT, on ? 1 : 0); }

// ============================== MQTT / PUBLICAR ================================
static const char* estado_to_str(estado_t e) {
    switch (e) {
        case EST_INIT: return "init";
        case EST_CERRADO: return "cerrado";
        case EST_ABIERTO: return "abierto";
        case EST_ABRIENDO: return "abriendo";
        case EST_CERRANDO: return "cerrando";
        case EST_PARADO: return "parado";
        case EST_EMERGENCIA: return "emergencia";
        default: return "error";
    }
}

static void mqtt_pub_estado(const char *origin) {
    if (!mqtt_client) return;
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"estado\":\"%s\",\"LSA\":%d,\"LSC\":%d,\"origen\":\"%s\"}",
             estado_to_str(g_estado), LSA_is_active(), LSC_is_active(),
             origin ? origin : "n/a");
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, payload, 0, 0, 0);
}

static void mqtt_pub_tele(void) {
    if (!mqtt_client) return;
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"lamp\":%d,\"MA\":%d,\"MC\":%d,\"LSA\":%d,\"LSC\":%d}",
             (int)g_lamp, gpio_get_level(GPIO_MA), gpio_get_level(GPIO_MC),
             LSA_is_active(), LSC_is_active());
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TELE, payload, 0, 0, 0);
}

// ============================== COLA / ENCOLAR =================================
static void push_event_isr(evento_t ev) {
    app_event_t e = {.type = ev};
    xQueueSendFromISR(g_evt_queue, &e, NULL);
}
static void push_event(evento_t ev) {
    app_event_t e = {.type = ev};
    xQueueSend(g_evt_queue, &e, 0);
}

// ============================== ISR BOTONES ====================================
static void IRAM_ATTR isr_btn_pp(void *arg) {
    int64_t now = esp_timer_get_time();
    if (now - isr_last_us_pp > DEBOUNCE_US) {
        push_event_isr(EV_BTN_PP); isr_last_us_pp = now;
    }
}
static void IRAM_ATTR isr_btn_abrir(void *arg) {
    int64_t now = esp_timer_get_time();
    if (now - isr_last_us_abrir > DEBOUNCE_US) {
        push_event_isr(EV_BTN_ABRIR); isr_last_us_abrir = now;
    }
}
static void IRAM_ATTR isr_btn_cerrar(void *arg) {
    int64_t now = esp_timer_get_time();
    if (now - isr_last_us_cerrar > DEBOUNCE_US) {
        push_event_isr(EV_BTN_CERRAR); isr_last_us_cerrar = now;
    }
}
static void IRAM_ATTR isr_btn_emerg(void *arg) {
    int64_t now = esp_timer_get_time();
    if (now - isr_last_us_emerg > DEBOUNCE_US) {
        push_event_isr(EV_BTN_EMERG); isr_last_us_emerg = now;
    }
}

// ============================== TIMER 50ms =====================================
static void timer50_cb(TimerHandle_t xTimer) {
    push_event(EV_TICK_50MS);
    if (LSA_is_active()) push_event(EV_LSA_ACTIVO);
    if (LSC_is_active()) push_event(EV_LSC_ACTIVO);

     // ---- Nuevo: disparador físico ABRIR cuando el pin pase de 1 -> 0 ----
    static int trig_prev = 1;
    int trig_now = gpio_get_level(GPIO_TRIG_ABRIR);
    if (trig_prev == 1 && trig_now == 0) {
        // Reutilizamos la misma ruta que "ABRIR" por MQTT
        push_event(EV_MQTT_ABRIR);
    }
    trig_prev = trig_now;
}

// ============================== LÁMPARA TASK ===================================
static void lamp_task(void *pv) {
    while (1) {
        switch (g_lamp) {
            case LAMP_OFF:
                gpio_set_level(GPIO_LAMP, 0);
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            case LAMP_ABRIENDO:   // 1 Hz
                gpio_set_level(GPIO_LAMP, 1); vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(GPIO_LAMP, 0); vTaskDelay(pdMS_TO_TICKS(500));
                break;
            case LAMP_CERRANDO:   // 2 Hz
                gpio_set_level(GPIO_LAMP, 1); vTaskDelay(pdMS_TO_TICKS(250));
                gpio_set_level(GPIO_LAMP, 0); vTaskDelay(pdMS_TO_TICKS(250));
                break;
            case LAMP_EMERG:      // 4 Hz
                gpio_set_level(GPIO_LAMP, 1); vTaskDelay(pdMS_TO_TICKS(125));
                gpio_set_level(GPIO_LAMP, 0); vTaskDelay(pdMS_TO_TICKS(125));
                break;
        }
    }
}

// ============================== FSM / LÓGICA ===================================
static void fsm_set_estado(estado_t nuevo, const char *origin) {
    if (g_estado == nuevo) {
        mqtt_pub_estado(origin);
        return;
    }
    g_estado = nuevo;

    switch (g_estado) {
        case EST_ABRIENDO:
            g_lamp = LAMP_ABRIENDO;
            set_emerg_pin(false);
            motor_abrir();
            g_prev_dir = EST_ABRIENDO;
            break;
        case EST_CERRANDO:
            g_lamp = LAMP_CERRANDO;
            set_emerg_pin(false);
            motor_cerrar();
            g_prev_dir = EST_CERRANDO;
            break;
        case EST_ABIERTO:
        case EST_CERRADO:
        case EST_PARADO:
            g_lamp = LAMP_OFF;
            set_emerg_pin(false);
            motor_parar();
            break;
        case EST_EMERGENCIA:
            g_lamp = LAMP_EMERG;
            set_emerg_pin(true);
            motor_parar();
            break;
        case EST_INIT:
        case EST_ERROR:
        default:
            g_lamp = LAMP_OFF;
            set_emerg_pin(false);
            motor_parar();
            break;
    }

    ESP_LOGI(TAG, "Estado -> %s", estado_to_str(g_estado));
    mqtt_pub_estado(origin);
}

static void fsm_handle_event(evento_t ev) {
    if (g_estado == EST_EMERGENCIA) {
        if (ev == EV_MQTT_RESET) {
            if (LSA_is_active()) fsm_set_estado(EST_ABIERTO, "reset");
            else if (LSC_is_active()) fsm_set_estado(EST_CERRADO, "reset");
            else fsm_set_estado(EST_PARADO, "reset");
        } else if (ev == EV_BTN_EMERG || ev == EV_MQTT_EMERG) {
            mqtt_pub_estado("emerg_repeat");
        }
        return;
    }

    switch (ev) {
        case EV_BTN_EMERG:
        case EV_MQTT_EMERG:
            fsm_set_estado(EST_EMERGENCIA, "emergencia"); return;

        case EV_BTN_ABRIR:
        case EV_MQTT_ABRIR:
            if (LSA_is_active()) fsm_set_estado(EST_ABIERTO, "abrir_bloqueado_LSA");
            else fsm_set_estado(EST_ABRIENDO, "abrir");
            return;

        case EV_BTN_CERRAR:
        case EV_MQTT_CERRAR:
            if (LSC_is_active()) fsm_set_estado(EST_CERRADO, "cerrar_bloqueado_LSC");
            else fsm_set_estado(EST_CERRANDO, "cerrar");
            return;

        case EV_BTN_PP:
        case EV_MQTT_PP:
            if (g_estado == EST_PARADO) {
                if (g_prev_dir == EST_ABRIENDO) fsm_set_estado(EST_CERRANDO, "pp_resume");
                else fsm_set_estado(EST_ABRIENDO, "pp_resume");
            } else if (g_estado == EST_ABRIENDO || g_estado == EST_CERRANDO) {
                fsm_set_estado(EST_PARADO, "pp_stop");
            } else if (g_estado == EST_ABIERTO) {
                fsm_set_estado(EST_CERRANDO, "pp_from_abierto");
            } else if (g_estado == EST_CERRADO) {
                fsm_set_estado(EST_ABRIENDO, "pp_from_cerrado");
            } else {
                fsm_set_estado(EST_PARADO, "pp_default");
            }
            return;

        case EV_MQTT_RESET:
            if (LSA_is_active()) fsm_set_estado(EST_ABIERTO, "reset_norm");
            else if (LSC_is_active()) fsm_set_estado(EST_CERRADO, "reset_norm");
            else fsm_set_estado(EST_PARADO, "reset_norm");
            return;

        case EV_LSA_ACTIVO:
            if (g_estado == EST_ABRIENDO) fsm_set_estado(EST_ABIERTO, "lsa_hit");
            return;

        case EV_LSC_ACTIVO:
            if (g_estado == EST_CERRANDO) fsm_set_estado(EST_CERRADO, "lsc_hit");
            return;

        default: return;
    }
}

static void fsm_task(void *pv) {
    if (LSA_is_active()) g_estado = EST_ABIERTO;
    else if (LSC_is_active()) g_estado = EST_CERRADO;
    else g_estado = EST_PARADO;

    mqtt_pub_estado("boot");

    app_event_t e;
    int tick_ms = 0;

    while (1) {
        if (xQueueReceive(g_evt_queue, &e, pdMS_TO_TICKS(200))) {
            if (e.type == EV_TICK_50MS) {
                tick_ms += 50;
                if (tick_ms >= 1000) { mqtt_pub_tele(); tick_ms = 0; }
            } else {
                fsm_handle_event(e.type);
            }
        }
    }
}

// ============================== MQTT CALLBACK =================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado");
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CMD, 0);
            mqtt_pub_estado("mqtt_connected");
            break;

        case MQTT_EVENT_DATA: {
            char cmd[32] = {0};
            int len = event->data_len > 31 ? 31 : event->data_len;
            memcpy(cmd, event->data, len);
            for (int i = 0; i < len; ++i) cmd[i] = (char)toupper((unsigned char)cmd[i]);

            ESP_LOGI(TAG, "CMD MQTT: %s", cmd);
            if (strncmp(cmd, "PP", 2) == 0) push_event(EV_MQTT_PP);
            else if (strncmp(cmd, "ABRIR", 5) == 0) push_event(EV_MQTT_ABRIR);
            else if (strncmp(cmd, "CERRAR", 6) == 0) push_event(EV_MQTT_CERRAR);
            else if (strncmp(cmd, "EMERGENCIA", 10) == 0 || strncmp(cmd, "EMERG", 5) == 0) push_event(EV_MQTT_EMERG);
            else if (strncmp(cmd, "RESET", 5) == 0) push_event(EV_MQTT_RESET);
            else ESP_LOGW(TAG, "Comando desconocido: %s", cmd);
        } break;

        default: break;
    }
}

static void mqtt_start(void) {
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// ============================== INIT GPIO =====================================
static void gpio_init_all(void) {
    // Salidas
    gpio_config_t outcfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<GPIO_MA) | (1ULL<<GPIO_MC) | (1ULL<<GPIO_LAMP) | (1ULL<<GPIO_EMERG_OUT),
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&outcfg);
    motor_parar(); gpio_set_level(GPIO_LAMP, 0); set_emerg_pin(false);

    // Entradas LSA/LSC con pull-up (si usas 34–39, pon pull-up externo)
    gpio_config_t incfg = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<GPIO_LSA) | (1ULL<<GPIO_LSC) | (1ULL<<GPIO_TRIG_ABRIR),
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&incfg);

    // Botones: pull-up + interrupción por flanco de bajada
    gpio_config_t btncfg = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<GPIO_BTN_PP) | (1ULL<<GPIO_BTN_ABRIR) | (1ULL<<GPIO_BTN_CERRAR) | (1ULL<<GPIO_BTN_EMERG),
        .pull_up_en = 1,  // 34–39 no tienen pull-up interno → usa resistencia externa
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&btncfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_BTN_PP,     isr_btn_pp, NULL);
    gpio_isr_handler_add(GPIO_BTN_ABRIR,  isr_btn_abrir, NULL);
    gpio_isr_handler_add(GPIO_BTN_CERRAR, isr_btn_cerrar, NULL);
    gpio_isr_handler_add(GPIO_BTN_EMERG,  isr_btn_emerg, NULL);
}

// ============================== Wi-Fi STA (propio) ============================
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect(); // reintentar
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &got_ip));

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char*)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char*)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Conectando a Wi-Fi SSID: %s ...", WIFI_SSID);
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi conectado");
}

// ============================== APP MAIN ======================================
void app_main(void) {
    ESP_LOGI(TAG, "Iniciando sistema portón...");

    // NVS, netif y loop por tu cuenta (no usamos protocol_examples_common)
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Wi-Fi
    wifi_init_sta();

    // GPIOs
    gpio_init_all();

    // Cola y timer
    g_evt_queue = xQueueCreate(16, sizeof(app_event_t));
    g_timer50 = xTimerCreate("t50", pdMS_TO_TICKS(50), pdTRUE, NULL, timer50_cb);
    xTimerStart(g_timer50, 0);

    // Tareas
    xTaskCreate(lamp_task, "lamp", 2048, NULL, 5, NULL);

    // MQTT
    mqtt_start();

    // FSM
    xTaskCreate(fsm_task, "fsm", 4096, NULL, 8, NULL);

}