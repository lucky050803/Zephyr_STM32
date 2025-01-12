/**
  ******************************************************************************
  * @file    main.c
  * @author  P. COURBIN
  * @version V2.0
  * @date    08-12-2023
  * @brief   WithGUI version (Corrected)
  ******************************************************************************
*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

#include "display.hpp"
#include "bme680.hpp"
#include "ESIEALogo.hpp"

#define STACKSIZE 4096
#define MESSAGE_QUEUE_SIZE 10
#define MESSAGE_MAX_LEN 50
#define PRIO_DISPLAY_TASK 1
#define PRIO_MSG_TASK 3
#define PRIO_HELLO_TASK 4
#define PRIO_BME680_TASK 2
#define PERIOD_DISPLAY_TASK 1000
#define PERIOD_BME680_TASK 500
#define PERIOD_HELLO_TASK 1000

static K_THREAD_STACK_DEFINE(display_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(bme680_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(msg_stack, STACKSIZE);
static K_THREAD_STACK_DEFINE(hello_stack, STACKSIZE);

struct data_item_type {
    char text[MESSAGE_MAX_LEN];
};

static char my_msgq_buffer[MESSAGE_QUEUE_SIZE * sizeof(struct data_item_type)];
static struct k_msgq my_msgq;

const struct device *bme680_dev = DEVICE_DT_GET(DT_CHOSEN(perso_bme680));
const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

myDisplay display;
myBME680 bme680;

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

static void display_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_DISPLAY_TASK);
    struct data_item_type msg;

    while (1)
    {
        TASK_START();

        display.task_handler();
        display.chart_add_temperature(bme680.get_temperature());
        display.chart_add_humidity(bme680.get_humidity());

        snprintf(msg.text, sizeof(msg.text), "display task: %d.%02d - %d.%02d", 
                 bme680.temperature.val1, bme680.temperature.val2 / 10000, 
                 bme680.humidity.val1, bme680.humidity.val2 / 10000);

        if (k_msgq_put(&my_msgq, &msg, K_NO_WAIT) != 0) {
            LOG_WRN("Message queue full. Message dropped.");
        }

        TASK_END();
    }
}

static void bme680_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_BME680_TASK);

    while (1)
    {
        TASK_START();

        bme680.update_values();
        LOG_INF("Temperature: %d.%06d °C, Humidity: %d.%06d %%", 
                bme680.temperature.val1, bme680.temperature.val2 / 10000,
                bme680.humidity.val1, bme680.humidity.val2 / 10000);

        TASK_END();
    }
}

static void hello_task(void *p1, void *p2, void *p3)
{
    TASK_PREPARE(PERIOD_HELLO_TASK);
    struct data_item_type msg;

    while (1)
    {
        TASK_START();

        snprintf(msg.text, sizeof(msg.text), "hello task: hello\n");
        if (k_msgq_put(&my_msgq, &msg, K_NO_WAIT) != 0) {
            LOG_WRN("Message queue full. Message dropped.");
            k_msgq_purge(&my_msgq);
        }

        TASK_END();
    }
}

int main(void)
{
    struct k_thread display_t;
    k_tid_t display_tid;
    struct k_thread bme680_t;
    k_tid_t bme680_tid;
    struct k_thread msg_t;
    k_tid_t msg_tid;
    struct k_thread hello_t;
    k_tid_t hello_tid;

    k_msgq_init(&my_msgq, my_msgq_buffer, sizeof(struct data_item_type), MESSAGE_QUEUE_SIZE);

    display.init(display_dev, true);
    bme680.init(bme680_dev);

    display_tid = k_thread_create(&display_t, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
                                  display_task, NULL, NULL, NULL,
                                  PRIO_DISPLAY_TASK, 0, K_NO_WAIT);
    k_thread_name_set(display_tid, "display");

    bme680_tid = k_thread_create(&bme680_t, bme680_stack, K_THREAD_STACK_SIZEOF(bme680_stack),
                                 bme680_task, NULL, NULL, NULL,
                                 PRIO_BME680_TASK, 0, K_NO_WAIT);
    k_thread_name_set(bme680_tid, "bme680");

    msg_tid = k_thread_create(&msg_t, msg_stack, K_THREAD_STACK_SIZEOF(msg_stack),
                              message_display_task, NULL, NULL, NULL,
                              PRIO_MSG_TASK, 0, K_NO_WAIT);
    k_thread_name_set(msg_tid, "msg");

    hello_tid = k_thread_create(&hello_t, hello_stack, K_THREAD_STACK_SIZEOF(hello_stack),
                                hello_task, NULL, NULL, NULL,
                                PRIO_HELLO_TASK, 0, K_NO_WAIT);
    k_thread_name_set(hello_tid, "hello");

    return 0;
}