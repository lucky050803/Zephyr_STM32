/**
 ******************************************************************************
 * @file    main.c
 * @author  P. COURBIN
 * @version V2.0
 * @date    08-12-2023
 * @brief   Readers/Writers with dynamic writer addition using button
 ******************************************************************************
*/

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

#include "display.hpp"
#include "bme680.hpp"
#include "ESIEALogo.hpp"

#define STACKSIZE 4096
#define MESSAGE_QUEUE_SIZE 10
#define MESSAGE_MAX_LEN 50
#define PRIO_DISPLAY_TASK 4
#define PRIO_MSG_TASK 1
#define PRIO_BME680_TASK 3
#define PRIO_WRITER_TASK 2
#define PERIOD_DISPLAY_TASK 1000
#define PERIOD_BME680_TASK 500

#define MAX_WRITERS 5

static K_THREAD_STACK_DEFINE(display_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(bme680_stack, STACKSIZE);
static K_THREAD_STACK_ARRAY_DEFINE(writer_stacks, MAX_WRITERS, STACKSIZE);

struct data_item_type {
    char text[MESSAGE_MAX_LEN];
};

static char my_msgq_buffer[MESSAGE_QUEUE_SIZE * sizeof(struct data_item_type)];
static struct k_msgq my_msgq;

const struct device *bme680_dev = DEVICE_DT_GET(DT_CHOSEN(perso_bme680));
const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

myDisplay display;
myBME680 bme680;

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;

K_MUTEX_DEFINE(writer_mutex);
int current_writer_count = 0;

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
    LOG_DBG("START task %s", name); \
    start = k_uptime_get_32();

#define TASK_END() \
    LOG_DBG("END task %s - %dms", name, k_uptime_get_32() - start);

static void message_display_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_DISPLAY_TASK);
    struct data_item_type msg;

    while (1)
    {
        TASK_START();

        if (k_msgq_get(&my_msgq, &msg, K_MSEC(100)) == 0) {
            display.text_add(msg.text);
        } else {
            LOG_WRN("Message queue timeout");
        }

        TASK_END();
    }
}

static void writer_task(void *p1, void *p2, void *p3)
{
    int writer_id = (int)(intptr_t)p1;
    struct data_item_type msg;

    while (1) {
        snprintf(msg.text, sizeof(msg.text), "Writer %d: writing a message", writer_id);
        if (k_msgq_put(&my_msgq, &msg, K_NO_WAIT) != 0) {
            LOG_WRN("Message queue full. Message dropped.");
        }
        k_msleep(2000);
    }
}

void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{   
    TASK_PREPARE(PERIOD_DISPLAY_TASK);
    
    LOG_INF("Button pressed");
  
        TASK_START();
        if (current_writer_count < MAX_WRITERS) {
            int writer_id = current_writer_count++;
            struct k_thread writer_kid;
            k_tid_t writer_tid;
            k_thread_create(&writer_kid, writer_stacks[writer_id], K_THREAD_STACK_SIZEOF(writer_stacks[writer_id]),
                            writer_task, (void *)(intptr_t)writer_id, NULL, NULL,
                            PRIO_WRITER_TASK, 0, K_NO_WAIT);
            k_thread_name_set(writer_tid, "wr");
            LOG_INF("Writer %d added", writer_id);
        } else {
            LOG_WRN("Max writers reached");
        }
        TASK_END();
    
    
}

static void bme680_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_BME680_TASK);

    while (1)
    {
        TASK_START()
        
        bme680.update_values();
        LOG_INF("Temperature: %d.%06d °C, Humidity: %d.%06d %%", 
                bme680.temperature.val1, bme680.temperature.val2 / 10000,
                bme680.humidity.val1, bme680.humidity.val2 / 10000);

        TASK_END()
    }
}

int main(void)
{
    struct k_thread display_t;
    k_tid_t display_tid;
    struct k_thread bme680_t;
    k_tid_t bme680_tid;

    k_msgq_init(&my_msgq, my_msgq_buffer, sizeof(struct data_item_type), MESSAGE_QUEUE_SIZE);

    display.init(display_dev, true);
    bme680.init(bme680_dev);

    if (!device_is_ready(button.port)) {
        LOG_ERR("Button device not ready");
        return -1;
    }

    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    display_tid = k_thread_create(&display_t, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
                                  message_display_task, NULL, NULL, NULL,
                                  PRIO_DISPLAY_TASK, 0, K_NO_WAIT);
    k_thread_name_set(display_tid, "display");

    bme680_tid = k_thread_create(&bme680_t, bme680_stack, K_THREAD_STACK_SIZEOF(bme680_stack),
                                 bme680_task, NULL, NULL, NULL,
                                 PRIO_BME680_TASK, 0, K_NO_WAIT);
    k_thread_name_set(bme680_tid, "bme680");

    return 0;
}
