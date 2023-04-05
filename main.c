#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define CONFIG_KNOB_PIN_ORDER_SW 0
#define CONFIG_KNOB_PIN_ORDER_DT 1
#define CONFIG_KNOB_PIN_ORDER_CLK 2

// stack word size
#define configSTACK_DEPTH_TYPE uint32_t
#define CONFIG_KNOB_TASK_STACK_SIZE 12000
// knob params
#define CONFIG_KNOB_NUMBER 2
#define CONFIG_KNOB_STEP 2.5
#define CONFIG_KNOB_DOUBLE_CLICK                                               \
  0 // put to 1 to have press and release actionned
#define INIT_VOLUME 0
// delay for readknob's infinite loop
const TickType_t delay = pdMS_TO_TICKS(10);

// pin order : {sw,dt,clk}
static int pin_arr[CONFIG_KNOB_NUMBER][3] = {
    {       // knob 1
      36,   // sw1
      34,   // dt1
      39    // clk1
    },
    {       // knob 2
      35,   // sw2
      32,   // dt2
      33    // clk2
    }
    };

typedef struct {
  // we store entire history of knob inputs in a 32 bit int
  uint32_t sw;
  uint32_t dt;
  uint32_t clk;
  int pin_sw;
  int pin_dt;
  int pin_clk;
} knob;

int get_ones(uint64_t b) {
  if (b == 0)
    return 0;
  else
    return b % 2 + get_ones(b / 2);
}

knob *knob_malloc(int n) {
  knob *k = NULL;
  if (n > 0)
    k = (knob *)pvPortMalloc(n * sizeof(knob));
  else {
    printf("number of knobs must be a strictly positive integer");
    exit(-1);
  }
  return k;
}

void knob_set_pins(int pin_array[][3], knob *k) { // puts pins in knob struct
  for (int i = 0; i < CONFIG_KNOB_NUMBER; i++) {  // iterates over array
    k[i].pin_sw = pin_array[i][CONFIG_KNOB_PIN_ORDER_SW];
    k[i].pin_dt = pin_array[i][CONFIG_KNOB_PIN_ORDER_DT];
    k[i].pin_clk = pin_array[i][CONFIG_KNOB_PIN_ORDER_CLK];
  }
}

bool is_high(uint32_t a) { // checks if mostright bit is high ##to remove if not used
  return a & 0x00000001;
}

bool cmp_n_m(uint32_t a, uint32_t b, uint32_t n,
             uint32_t m) { // compares (bit indexed n of a) and (bit indexed m of b) (n is 0-indexed)
             //returns 1 if theyre the same
  return !(((a >> n) ^ (b >> m)) & 1);
}

bool read_n(uint32_t a, uint32_t n) { // reads bit indexed n (n is 0-indexed)
  return (a >> n) & 1;
}

bool read_current(uint32_t m) { return read_n(m, 0); }

bool has_changed(uint32_t j) { return !cmp_n_m(j, j, 1, 0); }

void update_array(knob *p) { // updates each knob's values
  for (int i = 0; i < CONFIG_KNOB_NUMBER; i++) {
    (p + i)->dt = ((p + i)->dt << 1) | gpio_get_level((p + i)->pin_dt); // leftshift then change mostright bit
    (p + i)->clk = ((p + i)->clk << 1) | gpio_get_level((p + i)->pin_clk);
    (p + i)->sw = ((p + i)->sw << 1) | gpio_get_level((p + i)->pin_sw);
  }
}

uint64_t knob_get_mask(int pin_arr[][3]) {
  // creates bitmask for pin initialization from knob array
  uint64_t pin_mask = 0;
  for (int i = 0; i < CONFIG_KNOB_NUMBER; i++) { // iterates over array
    pin_mask = pin_mask | (1ULL << pin_arr[i][0]);
    pin_mask = pin_mask | (1ULL << pin_arr[i][1]);
    pin_mask = pin_mask | (1ULL << pin_arr[i][2]);
  }
  return pin_mask;
}

// pin init

void knob_task(void *pvparams) {

  float counter = INIT_VOLUME;
  knob *k_pointer = NULL;
  k_pointer = knob_malloc(CONFIG_KNOB_NUMBER);
  // pin stuff
  knob_set_pins(pin_arr, k_pointer);

  // get past reading for 1st time (cant put to 0 or 1 cuz the thing stores past
  // states mecanically)
  update_array(k_pointer);
  int j; // iterator on k_pointer

  while (1) {                // infinite loop
    update_array(k_pointer); // get current readings
    for (j = 0; j < CONFIG_KNOB_NUMBER; j++) {
      if (has_changed(k_pointer[j].sw) &&
          (read_n(k_pointer[j].sw, 0) | CONFIG_KNOB_DOUBLE_CLICK)) {
        printf("Button pressed\n");
      }
      if (has_changed(k_pointer[j].clk)) { // past_reading.clk != current.clk
        if (!cmp_n_m(k_pointer[j].dt, k_pointer[j].clk, 1,0)) { // past_reading.dt != current.clk
          counter += CONFIG_KNOB_STEP;
          printf("volume ↑ (knob %d): %f\t", j, counter);
          printf("highwater = %d octet\n", uxTaskGetStackHighWaterMark(NULL));
        }
        if (cmp_n_m(k_pointer[j].dt, k_pointer[j].clk, 1,
                    0)) { // past_reading.dt == current.clk
          counter -= CONFIG_KNOB_STEP;
          printf("volume ↓ (knob %d): %f\t", j, counter);
          printf("highwater = %d octet\n", uxTaskGetStackHighWaterMark(NULL));
        }
      }
    }
    vTaskDelay(delay);
  }
}

void app_main(void) {

  // init pins in knob task

  // gpio config
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = knob_get_mask(pin_arr);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  // start task
  xTaskCreate(knob_task, "main_read", CONFIG_KNOB_TASK_STACK_SIZE, NULL, 2,
              NULL);
  // for (;;) {}
}
