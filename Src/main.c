/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : SPI slave + ADC mic (MAX9814) + button
 *                   When button pressed: read mic via ADC, send audio over SPI
 *                   When button released: send 0xFF (idle marker)
 ******************************************************************************
 */

#include <stdint.h>

// RCC
#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830)
#define RCC_APB2ENR   (*(volatile uint32_t *)0x40023844)

// GPIOA
#define GPIOA_MODER   (*(volatile uint32_t *)0x40020000)
#define GPIOA_AFRL    (*(volatile uint32_t *)0x40020020)

// GPIOC (button on PC13)
#define GPIOC_IDR     (*(volatile uint32_t *)0x40020810)

// SPI1
#define SPI1_CR1      (*(volatile uint32_t *)0x40013000)
#define SPI1_CR2      (*(volatile uint32_t *)0x40013004)
#define SPI1_SR       (*(volatile uint32_t *)0x40013008)
#define SPI1_DR       (*(volatile uint32_t *)0x4001300C)

#define SPI_SR_RXNE   (1 << 0)

// ADC1 (PA0 = ADC1 channel 0)
#define ADC1_SR       (*(volatile uint32_t *)0x40012000)
#define ADC1_CR1      (*(volatile uint32_t *)0x40012004)
#define ADC1_CR2      (*(volatile uint32_t *)0x40012008)
#define ADC1_SMPR2    (*(volatile uint32_t *)0x40012010)
#define ADC1_SQR3     (*(volatile uint32_t *)0x40012034)
#define ADC1_DR       (*(volatile uint32_t *)0x4001204C)

// Idle marker — Pi sees this when button is not pressed
#define IDLE_MARKER   0xFF

static uint8_t adc_read_8bit(void) {
    // Start conversion
    ADC1_CR2 |= (1 << 30);            // SWSTART = 1

    // Wait for conversion complete
    while (!(ADC1_SR & (1 << 1)));     // wait for EOC bit

    // Read 12-bit result, shift to 8-bit (0-254 range, reserve 0xFF for idle)
    uint32_t raw = ADC1_DR & 0xFFF;    // 12-bit value (0-4095)
    uint8_t sample = (uint8_t)(raw >> 4);  // top 8 bits (0-255)

    // Clamp to 0-254 so 0xFF is always the idle marker
    if (sample == 0xFF) sample = 0xFE;

    return sample;
}

int main(void) {
    // --- Enable clocks ---
    RCC_AHB1ENR |= (1 << 0) | (1 << 2);  // GPIOA + GPIOC
    RCC_APB2ENR |= (1 << 8) | (1 << 12);  // ADC1 + SPI1

    // --- Configure PA0 as analog input (ADC) ---
    // MODER bits [1:0] = 11 (analog mode)
    GPIOA_MODER |= (3 << 0);

    // --- Configure ADC1 ---
    ADC1_CR1 = 0;                      // 12-bit resolution, no scan
    ADC1_CR2 = 0;
    ADC1_SMPR2 |= (3 << 0);           // Channel 0 sample time = 56 cycles
    ADC1_SQR3 = 0;                     // First conversion = channel 0

    // Enable ADC
    ADC1_CR2 |= (1 << 0);             // ADON = 1
    for (volatile int i = 0; i < 1000; i++);  // startup delay

    // --- Configure PA5 (SCK), PA6 (MISO), PA7 (MOSI) as AF5 (SPI1) ---
    GPIOA_MODER &= ~((3 << 10) | (3 << 12) | (3 << 14));
    GPIOA_MODER |=  ((2 << 10) | (2 << 12) | (2 << 14));

    GPIOA_AFRL &= ~((0xF << 20) | (0xF << 24) | (0xF << 28));
    GPIOA_AFRL |=  ((5 << 20) | (5 << 24) | (5 << 28));

    // --- Configure SPI1 as slave ---
    SPI1_CR1 = 0;
    SPI1_CR1 |= (1 << 9);             // SSM = 1
    SPI1_CR2 = 0;
    SPI1_DR = IDLE_MARKER;             // preload idle
    SPI1_CR1 |= (1 << 6);             // SPE = 1

    // --- Main loop ---
    uint8_t button_stable = 0;  // debounced button state: 0=released, 1=pressed
    uint32_t debounce_count = 0;
    #define DEBOUNCE_THRESHOLD 5000  // number of loop iterations to confirm state change

    while (1) {
        // Debounce: only change state after consistent readings
        uint8_t raw = !(GPIOC_IDR & (1 << 13));  // 1 = pressed, 0 = released
        if (raw == button_stable) {
            debounce_count = 0;
        } else {
            debounce_count++;
            if (debounce_count >= DEBOUNCE_THRESHOLD) {
                button_stable = raw;
                debounce_count = 0;
            }
        }

        if (SPI1_SR & SPI_SR_RXNE) {
            (void)SPI1_DR;

            if (button_stable) {
                SPI1_DR = adc_read_8bit();
            } else {
                SPI1_DR = IDLE_MARKER;
            }
        }
    }
}
