#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"

// Etiqueta para los logs
static const char *TAG = "TIMER_LED";

// Definimos el pin del LED
#define LED_PIN 2

// Intervalo en milisegundos
#define TIMER_INTERVAL_MS 100

// Declaramos el timer handle global
static TimerHandle_t blink_timer = NULL;

// Estado del LED
static bool led_state = false;

// --------------------------------------------------
// Función callback: se ejecuta cada vez que vence el timer
// --------------------------------------------------
void blink_callback(TimerHandle_t xTimer) {
    led_state = !led_state;  // Cambia el estado
    gpio_set_level(LED_PIN, led_state);  // Aplica al pin
    ESP_LOGI(TAG, "Timer ejecutado → LED %s", led_state ? "ENCENDIDO" : "APAGADO");
}

// --------------------------------------------------
// Configuración del timer con FreeRTOS
// --------------------------------------------------
esp_err_t set_timer(void) {
    ESP_LOGI(TAG, "Inicializando timer...");

    // Convertimos milisegundos a ticks
    const TickType_t xInterval = pdMS_TO_TICKS(TIMER_INTERVAL_MS);

    // Creamos el timer
    blink_timer = xTimerCreate(
        "BlinkTimer",        // Nombre (solo para referencia)
        xInterval,           // Periodo del timer
        pdTRUE,              // Auto-reload (TRUE = se repite)
        (void *)0,           // ID (no usado)
        blink_callback       // Función callback
    );

    // Validamos que se haya creado correctamente
    if (blink_timer == NULL) {
        ESP_LOGE(TAG, "Error: no se pudo crear el timer.");
        return ESP_FAIL;
    }

    // Iniciamos el timer
    if (xTimerStart(blink_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Error al iniciar el timer.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Timer iniciado correctamente cada %d ms", TIMER_INTERVAL_MS);
    return ESP_OK;
}

// --------------------------------------------------
// Función principal (app_main)
// --------------------------------------------------
void app_main(void) {
    // Configurar pin del LED
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // Inicializamos el timer
    if (set_timer() == ESP_OK) {
        ESP_LOGI(TAG, "Sistema en ejecución...");
    } else {
        ESP_LOGE(TAG, "Fallo en la configuración del timer.");
    }

    // No es necesario usar delays, FreeRTOS se encarga del tiempo
    // El sistema seguirá corriendo otras tareas si las hubiera
}
