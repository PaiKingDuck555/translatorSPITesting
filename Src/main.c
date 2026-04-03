/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Timer-driven ADC mic sampling + SPI slave + button
 *                   TIM2 triggers ADC at 16 kHz into a ring buffer.
 *                   SPI sends buffered samples to Pi on request.
 ******************************************************************************
 */

#include <stdint.h>

// RCC
#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830)
#define RCC_APB1ENR   (*(volatile uint32_t *)0x40023840)
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

// ADC1
#define ADC1_SR       (*(volatile uint32_t *)0x40012000)
#define ADC1_CR1      (*(volatile uint32_t *)0x40012004)
#define ADC1_CR2      (*(volatile uint32_t *)0x40012008)
#define ADC1_SMPR2    (*(volatile uint32_t *)0x40012010)
#define ADC1_SQR3     (*(volatile uint32_t *)0x40012034)
#define ADC1_DR       (*(volatile uint32_t *)0x4001204C)

// TIM2 (APB1, used to trigger ADC at 16 kHz)
#define TIM2_CR1      (*(volatile uint32_t *)0x40000000)
#define TIM2_DIER     (*(volatile uint32_t *)0x4000000C)
#define TIM2_SR       (*(volatile uint32_t *)0x40000010)
#define TIM2_PSC      (*(volatile uint32_t *)0x40000028)
#define TIM2_ARR      (*(volatile uint32_t *)0x4000002C)

// NVIC
#define NVIC_ISER0    (*(volatile uint32_t *)0xE000E100)

// Ring buffer for ADC samples
#define BUFFER_SIZE 4096
static volatile uint16_t audio_buffer[BUFFER_SIZE];
static volatile uint32_t buf_write = 0;  // ISR writes here
static volatile uint32_t buf_read = 0;   // main loop reads here
static volatile uint8_t recording = 0;   // 1 = button held, sampling active

// Idle marker
#define IDLE_MARKER   0xFF

// Button debounce
static uint8_t button_stable = 0;
static uint32_t debounce_count = 0;
#define DEBOUNCE_THRESHOLD 5000

// TIM2 interrupt handler — called at 16 kHz
void TIM2_IRQHandler(void) {
    TIM2_SR = 0;  // clear interrupt flag

    if (!recording) return;

    // Start ADC conversion
    ADC1_CR2 |= (1 << 30);  // SWSTART
    while (!(ADC1_SR & (1 << 1)));  // wait EOC

    uint16_t sample = ADC1_DR & 0xFFF;  // 12-bit value

    // Store in ring buffer
    uint32_t next = (buf_write + 1) % BUFFER_SIZE;
    if (next != buf_read) {  // don't overwrite unread data
        audio_buffer[buf_write] = sample;
        buf_write = next;
    }
}

int main(void) {
    // --- Enable clocks ---
    RCC_AHB1ENR |= (1 << 0) | (1 << 2);   // GPIOA + GPIOC
    RCC_APB1ENR |= (1 << 0);               // TIM2
    RCC_APB2ENR |= (1 << 8) | (1 << 12);   // ADC1 + SPI1

    // --- PA0 = analog input (ADC channel 0) ---
    GPIOA_MODER |= (3 << 0);

    // --- Configure ADC1 ---
    ADC1_CR1 = 0;
    ADC1_CR2 = 0;
    ADC1_SMPR2 = (3 << 0);   // channel 0: 56 cycles sample time
    ADC1_SQR3 = 0;            // first conversion = channel 0
    ADC1_CR2 |= (1 << 0);    // ADON
    for (volatile int i = 0; i < 1000; i++);

    // --- Configure TIM2 for 16 kHz interrupt ---
    // CPU = 16 MHz (HSI default)
    // 16,000,000 / 16,000 = 1000
    // PSC = 0, ARR = 999 → interrupt every 1000 clocks = 16 kHz
    TIM2_PSC = 0;
    TIM2_ARR = 999;
    TIM2_DIER |= (1 << 0);   // enable update interrupt
    NVIC_ISER0 |= (1 << 28); // enable TIM2 IRQ (position 28)
    TIM2_CR1 |= (1 << 0);    // start timer

    // --- Configure SPI1 (PA5=SCK, PA6=MISO, PA7=MOSI) ---
    GPIOA_MODER &= ~((3 << 10) | (3 << 12) | (3 << 14));
    GPIOA_MODER |=  ((2 << 10) | (2 << 12) | (2 << 14));
    GPIOA_AFRL &= ~((0xF << 20) | (0xF << 24) | (0xF << 28));
    GPIOA_AFRL |=  ((5 << 20) | (5 << 24) | (5 << 28));

    SPI1_CR1 = 0;
    SPI1_CR1 |= (1 << 9);    // SSM = 1
    SPI1_CR2 = 0;
    SPI1_DR = IDLE_MARKER;
    SPI1_CR1 |= (1 << 6);    // SPE = 1

    // --- Main loop ---
    while (1) {
        // Debounce button (PC13, active LOW)
        uint8_t raw = !(GPIOC_IDR & (1 << 13));
        if (raw == button_stable) {
            debounce_count = 0;
        } else {
            debounce_count++;
            if (debounce_count >= DEBOUNCE_THRESHOLD) {
                button_stable = raw;
                debounce_count = 0;
                if (!button_stable) {
                    // Button released — reset buffer
                    recording = 0;
                } else {
                    // Button pressed — start recording
                    buf_write = 0;
                    buf_read = 0;
                    recording = 1;
                }
            }
        }

        // SPI: respond to Pi polls
        if (SPI1_SR & SPI_SR_RXNE) {
            (void)SPI1_DR;  // discard Pi's byte

            if (button_stable && buf_read != buf_write) {
                // Send next sample from ring buffer as 2 bytes
                // High byte first, then low byte on next poll
                uint16_t sample = audio_buffer[buf_read];
                buf_read = (buf_read + 1) % BUFFER_SIZE;

                // Send high byte (top 8 bits of 12-bit sample)
                SPI1_DR = (uint8_t)(sample >> 4);
            } else {
                SPI1_DR = IDLE_MARKER;
            }
        }
    }
}
