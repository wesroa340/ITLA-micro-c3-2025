#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "NVS_COLOR";

// Simulación de LED por consola
void set_led_color(uint8_t color) {
    const char *colors[] = {"Rojo", "Verde", "Azul", "Amarillo", "Morado", "Cian", "Blanco"};
    ESP_LOGI(TAG, "Color actual: %s", colors[color % 7]);
}

// Inicializar NVS
esp_err_t init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "NVS inicializado correctamente");
    }
    return err;
}

// Leer valor desde NVS
esp_err_t read_nvs_uint8(nvs_handle_t handle, const char *key, uint8_t *value) {
    esp_err_t err = nvs_get_u8(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No se encontró la clave '%s' en NVS", key);
        *value = 0;  // Valor por defecto
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error leyendo '%s': %s", key, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Valor leído de NVS (%s): %d", key, *value);
    }
    return err;
}

// Escribir valor en NVS
esp_err_t write_nvs_uint8(nvs_handle_t handle, const char *key, uint8_t value) {
    esp_err_t err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Valor %d guardado en clave '%s'", value, key);
    } else {
        ESP_LOGE(TAG, "Error escribiendo '%s': %s", key, esp_err_to_name(err));
    }
    return err;
}

void app_main(void) {
    ESP_ERROR_CHECK(init_nvs());

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(err));
        return;
    }

    uint8_t color = 0;
    read_nvs_uint8(my_handle, "color_led", &color);

    while (1) {
        set_led_color(color);
        vTaskDelay(pdMS_TO_TICKS(1000));

        color = (color + 1) % 7;  // Avanza al siguiente color
        write_nvs_uint8(my_handle, "color_led", color);
    }

    nvs_close(my_handle);
}
