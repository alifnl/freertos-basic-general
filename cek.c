#include <stdio.h>
#include "driver/gpio.h"

//library for vtask delay
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//library for spiffs
#include "esp_spiffs.h"

//library for logging
#include "esp_log.h"
//library for error handling
#include "esp_err.h"

//library for general purpose timer
#include "driver/gptimer.h"
#include "freertos/queue.h" 

#define DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))


static const char* TAG = "FileSystem";
esp_err_t result;
int led_state = 1;

typedef struct {
    int led_state_pass;
} example_queue_element_t;
QueueHandle_t queue_global;

static bool IRAM_ATTR example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    
    // Retrieve the count value from event data
    if(led_state == 1) {
        led_state = 0;
    }else{
        led_state = 1;
    }
    gpio_set_level(GPIO_NUM_22, led_state);
    example_queue_element_t ele = {
        .led_state_pass = led_state,
    };
    //code below to update alarm value dynamically
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + (led_state == 1 ? 500000 : 1000000)
    };
    gptimer_set_alarm_action(timer, &alarm_config);
    // Optional: send the event data to other task by OS queue
    // Do not introduce complex logics in callbacks
    // Suggest dealing with event data in the main loop, instead of in this callback
    xQueueSendFromISR(queue, &ele, &high_task_awoken);
    // return whether we need to yield at the end of ISR
    

    
    return high_task_awoken == pdTRUE;
}

void spiffs_setup();
void periodic_alarm_setup();
void one_shot_alarm_setup();

void app_main(void)
{
    //set GPIO22 as output
    gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT);

    //creating queue element
    example_queue_element_t ele;
    queue_global = xQueueCreate(10, sizeof(example_queue_element_t));

    if (!queue_global) {
        ESP_LOGE(TAG, "Creating queue failed");
        return;
    }

    

    //setup spiffs
    spiffs_setup();

    periodic_alarm_setup();
    while (true){

        BaseType_t ret = xQueueReceive(queue_global, &ele, 10);
        if (ret == pdTRUE) ESP_LOGI(TAG, "LED State=%d", ele.led_state_pass);
        DELAY(100); //for best result use delay 1/5 period less than the timer alarm
        

    }
}

void one_shot_alarm_setup(){

}
void periodic_alarm_setup(){
    //create general purpose timer handler
    ESP_LOGI(TAG, "Create timer handle");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };
    //create a new general purpose timer
    result = gptimer_new_timer(&timer_config, &gptimer);
    //handle if new timer failed 
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to initiate GPTimer (%s)", esp_err_to_name(result));
        return;
    }

    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb, // register user callback
    };

    gptimer_register_event_callbacks(gptimer, &cbs, queue_global);

    
    //made alarm stop event
    gptimer_alarm_config_t alarm_config1 = {
        .reload_count = 0, // counter will reload with 0 on alarm event
        .alarm_count = 500000 // period = 0.5s @resolution 1MHz
       // .flags.auto_reload_on_alarm = true, // enable auto-reload
       //if auto reload disable, will do one shot. or can update alarm value dynamically
    };

    //enable timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    
    //start timer, set alarm
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config1));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
}
void spiffs_setup(){
    //initiate config variable
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/storage",
        .partition_label = NULL, //if you have more than 1 partition
        .max_files = 5,
        .format_if_mount_failed = true // be careful not to lose important information
    };

    //register the configuration variable as spiffs
    result = esp_vfs_spiffs_register(&config);

    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(result));
        return;
    }

    //declare variable to know the total memory of the flash and used space
    size_t total = 0, used = 0;
    result = esp_spiffs_info(config.partition_label, &total, &used);
    if(result != ESP_OK){
        ESP_LOGE(TAG, "Failed to get partiton info (%s)", esp_err_to_name(result));
    }else{
        ESP_LOGE(TAG, "Partition size total; %d, used: %d", total, used);
    }

    //opening file
    FILE* f = fopen("/storage/myfile.txt", "r");
    if (f == NULL){
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    //read file
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    printf("%s\n", line);
}