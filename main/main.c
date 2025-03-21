#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include "ssd1306.h"
#include "gfx.h"

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

#define TRIGGER_PIN 17
#define ECHO_PIN 16

QueueHandle_t xQueueTime;           
SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueDistance;

void oled_init(void) {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);
    gpio_pull_up(BTN_2_OLED);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
    gpio_pull_up(BTN_3_OLED);
}

void pin_callback(uint gpio, uint32_t events) {
    absolute_time_t echo_start;
    absolute_time_t echo_end;

    if (events & GPIO_IRQ_EDGE_RISE) {
        echo_start = get_absolute_time(); 
        xQueueSend(xQueueTime, &echo_start, 0);
    }
    if (events & GPIO_IRQ_EDGE_FALL) {
        echo_end = get_absolute_time();
        xQueueSend(xQueueTime, &echo_end, 0);  
    }
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    while (true) {
        gpio_put(TRIGGER_PIN, 1);
        sleep_us(10);
        gpio_put(TRIGGER_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void echo_task(void *p) {
    absolute_time_t start_time, end_time;
    while (true) {
        if (xQueueReceive(xQueueTime, &start_time, portMAX_DELAY)) {
            if (xQueueReceive(xQueueTime, &end_time, pdMS_TO_TICKS(100))) {
                if (absolute_time_diff_us(start_time, end_time) > 0) {
                    int64_t dt_us = absolute_time_diff_us(start_time, end_time);
                    float distancia_cm = (dt_us * 0.0343f) / 2.0f;
                    printf("Distancia: %.2f cm\n", distancia_cm);
                    xQueueSend(xQueueDistance, &distancia_cm, 0);
                }
            }
        }
    }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    oled_init();

    float distancia_cm;
    while(1){
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &distancia_cm, pdMS_TO_TICKS(100))) {

                if (distancia_cm > 0 && distancia_cm < 400) {
                    gfx_clear_buffer(&disp);
                    char msg[20];
                    sprintf(msg, "Distancia: %.2f cm", distancia_cm);
                    gfx_draw_string(&disp, 0, 0, 1, msg);
                    int bar_len = (int)(distancia_cm / 2);
                    if (bar_len > 128) bar_len = 128;
                    gfx_draw_line(&disp, 0, 10, bar_len, 10);
                    gfx_show(&disp);
                    vTaskDelay(pdMS_TO_TICKS(150));

                } else {
                    gfx_clear_buffer(&disp);
                    gfx_draw_string(&disp, 0, 0, 1, "Falhou");
                    gfx_show(&disp);
                    vTaskDelay(pdMS_TO_TICKS(150));

                }
            }
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
}

int main() {
    stdio_init_all();

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

    xQueueTime = xQueueCreate(10, sizeof(absolute_time_t));
    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xQueueDistance = xQueueCreate(10, sizeof(float));

    xTaskCreate(trigger_task, "Trigger Task", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo Task", 256, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED Task", 512, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}