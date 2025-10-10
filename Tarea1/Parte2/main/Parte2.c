#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Pines para los LEDs
#define LED_R GPIO_NUM_25
#define LED_G GPIO_NUM_26
#define LED_B GPIO_NUM_33

// Delays de cada LED (ms)
#define DELAY_R 1000
#define DELAY_G 2000
#define DELAY_B 4000

// Configurar pines de salida
void init_leds(void) {
    gpio_reset_pin(LED_R);
    gpio_reset_pin(LED_G);
    gpio_reset_pin(LED_B);
    gpio_set_direction(LED_R, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_G, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_B, GPIO_MODE_OUTPUT);
}

// Tarea para LED rojo
void task_led_r(void *pvParameters) {
    while (1) {
        gpio_set_level(LED_R, 1);
        printf("LED Rojo encendido\n");
        vTaskDelay(pdMS_TO_TICKS(DELAY_R));
        gpio_set_level(LED_R, 0);
        printf("LED Rojo apagado\n");
        vTaskDelay(pdMS_TO_TICKS(DELAY_R));
    }
}

// Tarea para LED verde
void task_led_g(void *pvParameters) {
    while (1) {
        gpio_set_level(LED_G, 1);
        printf("LED Verde encendido\n");
        vTaskDelay(pdMS_TO_TICKS(DELAY_G));
        gpio_set_level(LED_G, 0);
        printf("LED Verde apagado\n");
        vTaskDelay(pdMS_TO_TICKS(DELAY_G));
    }
}

// Tarea para LED azul
void task_led_b(void *pvParameters) {
    while (1) {
        gpio_set_level(LED_B, 1);
        printf("LED Azul encendido\n");
        vTaskDelay(pdMS_TO_TICKS(DELAY_B));
        gpio_set_level(LED_B, 0);
        printf("LED Azul apagado\n");
        vTaskDelay(pdMS_TO_TICKS(DELAY_B));
    }
}

void app_main(void) {
    init_leds();
    printf("💡 Iniciando tareas FreeRTOS...\n");

    // Crear las tareas con stack de 2048 bytes
    xTaskCreate(task_led_r, "Task_LED_R", 2048, NULL, 1, NULL);
    xTaskCreate(task_led_g, "Task_LED_G", 2048, NULL, 1, NULL);
    xTaskCreate(task_led_b, "Task_LED_B", 2048, NULL, 1, NULL);

    // Bucle principal
    while (1) {
        printf("💬 Hello from main!\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
