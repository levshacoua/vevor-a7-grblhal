/*
  my_machine_map.h - VEVOR A7 10W laser (AtomStack YZ-102 / "LaserBox 40pro V1.0")

  ESP32-S3-WROOM-1U (N8R2) + single 74HC595 (bit-banged by stock fw, I2S-driven here)
  + 2x A4988-class drivers (U13=X, U14=Y).

  Hardware map reverse-engineered 2026-08-01/02 (see VEVOR_A7_HARDWARE_MAP.md):
    74HC595: SER=GPIO45, SRCLK=GPIO46, RCLK=GPIO14, OE->GND, SRCLR->VCC
    Register bits (Q index): 1=Y_DIR 2=Y_STEP 3=Y_EN 4=X_DIR 5=X_STEP 6=X_EN (EN active low)
    Limits: X=GPIO18 (min), Y=GPIO16 (min), active low, external pull-ups
    Laser TTL: GPIO38 (1kHz PWM)
    Hardware watchdog / charge pump: GPIO13 needs continuous ~8kHz square wave,
      rail charges ~10s (X) to ~2-3min (Y) from cold start. Fed by LEDC init in driver.c.
    Buzzer: GPIO6 (active high-ish, tone follows toggle frequency)
    SD card: data lines not identified yet; card-detect=GPIO48. SDCARD disabled.

  Part of grblHAL. Licensed under GPLv3 (same as parent project).
*/

#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "This board has an ESP32-S3 processor, select a corresponding build!"
#endif

#include "use_i2s_out.h"

#define BOARD_NAME "VEVOR A7 (AtomStack YZ-102)"

#define I2S_OUT_BCK             GPIO_NUM_46
#define I2S_OUT_WS              GPIO_NUM_14
#define I2S_OUT_DATA            GPIO_NUM_45

// X axis: register bits 5/4/6
#define X_STEP_PIN              I2SO(5)
#define X_DIRECTION_PIN         I2SO(4)
#define X_ENABLE_PIN            I2SO(6)
#define X_LIMIT_PIN             GPIO_NUM_18

// Y axis: register bits 2/1/3
#define Y_STEP_PIN              I2SO(2)
#define Y_DIRECTION_PIN         I2SO(1)
#define Y_ENABLE_PIN            I2SO(3)
#define Y_LIMIT_PIN             GPIO_NUM_16

// Z axis: mandatory in core; parked on non-existent register bits (single 595 = bits 0..7)
#define Z_STEP_PIN              I2SO(10)
#define Z_DIRECTION_PIN         I2SO(9)
#define Z_ENABLE_PIN            I2SO(8)
#define Z_LIMIT_PIN             GPIO_NUM_17

// Laser TTL on GPIO38
#define AUXOUTPUT0_PIN          GPIO_NUM_37 // Spindle PWM (laser TTL)
#define AUXOUTPUT1_PIN          I2SO(11)    // Dummy spindle enable (non-existent register bit)

#if DRIVER_SPINDLE_ENABLE & SPINDLE_PWM
#define SPINDLE_PWM_PIN         AUXOUTPUT0_PIN
#endif
#if DRIVER_SPINDLE_ENABLE & SPINDLE_ENA
#define SPINDLE_ENABLE_PIN      AUXOUTPUT1_PIN
#endif

// Probe connector (silk "Probe"; GPIO guess from pull-up group, verify before relying on it)
#define AUXINPUT0_PIN           GPIO_NUM_15
#if PROBE_ENABLE
#define PROBE_PIN               AUXINPUT0_PIN
#endif

// No physical control switches on this machine
#undef CONTROL_ENABLE
#define CONTROL_ENABLE 0

// TF (microSD) slot, SPI mode. Lines traced 2026-08-15 via slot ferrites:
//   L11 (slot 2, CS/DAT3)  -> GPIO0  (shares boot strap; external pull-up keeps CS idle high)
//   L12 (slot 3, CMD/MOSI) -> GPIO35
//   L14 (slot 5, CLK)      -> GPIO36
//   L13 (slot 7, DAT0/MISO)-> GPIO21 (default; GPIO47 is the fallback candidate,
//                             override with -DSPI_MISO_PIN=GPIO_NUM_47)
//   Card detect            -> GPIO48 (0 = card inserted)
// DAT1/DAT2 are not routed on this board.
#if SDCARD_ENABLE
#ifndef SPI_MISO_PIN
#define SPI_MISO_PIN            GPIO_NUM_21
#endif
#define SPI_MOSI_PIN            GPIO_NUM_35
#define SPI_SCK_PIN             GPIO_NUM_36
#define SD_CS_PIN               GPIO_NUM_0
#define SD_DETECT_PIN           GPIO_NUM_48
#endif
