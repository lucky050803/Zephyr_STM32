/**
 ******************************************************************************
 * @file    main.c
 * @author  P. COURBIN
 * @version V2.0
 * @date    08-12-2023
 * @brief   WithGUI version
 ******************************************************************************
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
LOG_MODULE_REGISTER(app);

#include "display.hpp"
#include "bme680.hpp"
#include "adc.hpp"
#define STACKSIZE (4096)
static K_THREAD_STACK_DEFINE(display_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(bme680_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(adc_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(led_stack, STACKSIZE);

#define PRIO_DISPLAY_TASK 4
#define PRIO_BME680_TASK 2
#define PRIO_ABC_TASK 1
#define PRIO_LED_TASK 3

#define PERIOD_DISPLAY_TASK 2000
#define PERIOD_BME680_TASK 500
#define PERIOD_LED_TASK 500

const struct device *bme680_dev = DEVICE_DT_GET(DT_CHOSEN(perso_bme680)); // OR DT_ALIAS(bme680)
const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

const int THRESHHOLD = 200;

myDisplay display;
myBME680 bme680;
myADC adc;

K_MUTEX_DEFINE(mutexLEDs);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define TASK_PREPARE(given_period)                    \
    k_tid_t tid = k_current_get();                    \
    const char *name = k_thread_name_get(tid);        \
    int period = given_period;                        \
    uint32_t start;                                   \
    struct k_timer timer;                             \
    k_timer_init(&timer, NULL, NULL);                 \
    k_timer_start(&timer, K_MSEC(0), K_MSEC(period)); \
    LOG_DBG("Run task %s - Priority %d - Period %d\n", name, k_thread_priority_get(tid), period);

#define TASK_START()                \
    k_timer_status_sync(&timer);    \
    LOG_INF("START task %s", name); \
    start = k_uptime_get_32();

#define TASK_END() \
    LOG_INF("END task %s - %dms", name, k_uptime_get_32() - start);

static void bme680_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_BME680_TASK);

    while (1)
    {
        
        TASK_START()

        bme680.update_values();

        TASK_END()
      
    }
}

static void adc_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_BME680_TASK);

    while (1)
    {
        
        TASK_START()

        adc.update_value();
        
        TASK_END()
      
    }

}

void update_leds(uint8_t led0_val)
{
    if (k_mutex_lock(&mutexLEDs, K_FOREVER) == 0)
    {
        gpio_pin_set_dt(&led0, led0_val);
        k_mutex_unlock(&mutexLEDs);
    }
}

uint8_t init_leds()
{
    uint8_t returned = 0;
    if (!device_is_ready(led0.port))
    {
        LOG_ERR("Error: LEDs devices are not ready (%s", led0.port->name);
        returned = -1;
    }

    if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE) < 0)
    {
        LOG_ERR("Error: LEDs config failed %s ", led0.port->name);
        returned = -2;
    }
    return returned;
}

static void led_task(void *p1, void *p2, void *p3)
{
   
    TASK_PREPARE(PERIOD_LED_TASK);
    int i, nbIter = 10000;
    while(1)
    {
     
        TASK_START();
        if (adc.get_value() > THRESHHOLD)
        {

            for (i = 0; i < nbIter; i++)
            {
                update_leds(1);
            }
            update_leds(0);
        }
        
        TASK_END();
    
    }
        
}

static void display_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_DISPLAY_TASK);
    char text[50] = {0};

    while (1)
    {
        
        TASK_START();

        display.task_handler();
        display.chart_add_temperature(bme680.get_temperature());
        display.chart_add_humidity(bme680.get_humidity());

        sprintf(text, "%d.%02d - %d.%02d", bme680.temperature.val1, bme680.temperature.val2 / 10000, bme680.humidity.val1, bme680.humidity.val2 / 10000);
        display.text_add(text);

        sprintf(text, "%d", adc.get_value());
        display.text_add(text);

        TASK_END();
      
    }
}

int main(void)
{
    struct k_thread display_t;
    k_tid_t display_tid;
    struct k_thread bme680_t;
    k_tid_t bme680_tid;
    struct k_thread adc_t;
    k_tid_t adc_tid;

    struct k_thread led_t;
    k_tid_t led_tid;
    display.init(display_dev, true);
    bme680.init(bme680_dev);
    adc.init();
    if (init_leds() < 0)
    {
        LOG_ERR("Error: %s", "LED or Switch init failed");
        return 0;
    }
    update_leds(0);
    display_tid = k_thread_create(&display_t, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
                                  display_task, NULL, NULL, NULL,
                                  PRIO_DISPLAY_TASK, 0, K_NO_WAIT);
    k_thread_name_set(display_tid, "display");

    bme680_tid = k_thread_create(&bme680_t, bme680_stack, K_THREAD_STACK_SIZEOF(bme680_stack),
                                 bme680_task, NULL, NULL, NULL,
                                 PRIO_BME680_TASK, 0, K_NO_WAIT);
    k_thread_name_set(bme680_tid, "bme680");

    adc_tid = k_thread_create(&adc_t, adc_stack, K_THREAD_STACK_SIZEOF(adc_stack),
                              adc_task, NULL, NULL, NULL,
                              PRIO_ABC_TASK, 0, K_NO_WAIT);
    k_thread_name_set(adc_tid, "adc");

    led_tid = k_thread_create(&led_t, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
                              led_task, NULL, NULL, NULL,
                              PRIO_LED_TASK, 0, K_NO_WAIT);
    k_thread_name_set(led_tid, "led");
}