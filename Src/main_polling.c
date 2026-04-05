/**
 ******************************************************************************
 * @file           : main_polling.c
 * @brief          : Auto-record 5s of audio from MAX9814 mic every 10 seconds,
 *                   stream over UART to Mac in real-time via ring buffer.
 *                   Uses 460800 baud to keep up with 16 kHz sample rate.
 *                   Polling mode for testing — no button required.
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

// ADC1 (PA0 = channel 0)
#define ADC1_SR       (*(volatile uint32_t *)0x40012000)
#define ADC1_CR1      (*(volatile uint32_t *)0x40012004)
#define ADC1_CR2      (*(volatile uint32_t *)0x40012008)
#define ADC1_SMPR2    (*(volatile uint32_t *)0x40012010)
#define ADC1_SQR3     (*(volatile uint32_t *)0x40012034)
#define ADC1_DR       (*(volatile uint32_t *)0x4001204C)

// TIM2 (16 kHz sample timer)
#define TIM2_CR1      (*(volatile uint32_t *)0x40000000)
#define TIM2_DIER     (*(volatile uint32_t *)0x4000000C)
#define TIM2_SR       (*(volatile uint32_t *)0x40000010)
#define TIM2_PSC      (*(volatile uint32_t *)0x40000028)
#define TIM2_ARR      (*(volatile uint32_t *)0x4000002C)

// USART2 (PA2=TX, goes through ST-Link USB to Mac)
#define USART2_SR     (*(volatile uint32_t *)0x40004400)
#define USART2_DR     (*(volatile uint32_t *)0x40004404)
#define USART2_BRR    (*(volatile uint32_t *)0x40004408)
#define USART2_CR1    (*(volatile uint32_t *)0x4000440C)

// NVIC
#define NVIC_ISER0    (*(volatile uint32_t *)0xE000E100)

// Ring buffer for streaming — ISR writes, main loop reads
#define RING_SIZE 2048
#define RING_MASK (RING_SIZE - 1)
static volatile uint16_t ring_buffer[RING_SIZE];
static volatile uint32_t ring_write = 0;

static volatile uint32_t sample_count = 0;
static volatile uint8_t  recording = 0;
static volatile uint32_t tick_counter = 0;

// 5 seconds of audio at 16 kHz = 80000 samples
#define RECORD_SAMPLES 80000

// 10 seconds at 16 kHz = 160000 ticks
#define POLL_TICKS 160000

// TIM2 ISR — called at 16 kHz
void TIM2_IRQHandler(void) {
    TIM2_SR = 0;
    tick_counter++;

    if (!recording) return;
    if (sample_count >= RECORD_SAMPLES) return;

    ADC1_CR2 |= (1 << 30);              // start conversion
    while (!(ADC1_SR & (1 << 1)));       // wait EOC
    ring_buffer[ring_write & RING_MASK] = ADC1_DR & 0xFFF;
    ring_write++;
    sample_count++;
}

static void uart_send_byte(uint8_t b) {
    while (!(USART2_SR & (1 << 7)));     // wait TXE
    USART2_DR = b;
}

static void uart_send_string(const char *s) {
    while (*s) uart_send_byte(*s++);
}

static void uart_send_decimal(uint32_t n) {
    char num[12];
    int len = 0;
    if (n == 0) {
        num[len++] = '0';
    } else {
        char tmp[12];
        int tlen = 0;
        while (n > 0) { tmp[tlen++] = '0' + (n % 10); n /= 10; }
        for (int i = tlen - 1; i >= 0; i--) num[len++] = tmp[i];
    }
    num[len] = 0;
    uart_send_string(num);
}

int main(void) {
    // --- Enable clocks ---
    RCC_AHB1ENR |= (1 << 0);              // GPIOA
    RCC_APB1ENR |= (1 << 0) | (1 << 17);  // TIM2 + USART2
    RCC_APB2ENR |= (1 << 8);               // ADC1

    // --- PA0 = analog (mic) ---
    GPIOA_MODER |= (3 << 0);

    // --- PA2 = USART2 TX (AF7) ---
    GPIOA_MODER &= ~(3 << 4);
    GPIOA_MODER |= (2 << 4);
    GPIOA_AFRL &= ~(0xF << 8);
    GPIOA_AFRL |= (7 << 8);

    // --- Configure USART2: 460800 baud ---
    // USARTDIV = 16MHz / (16 × 460800) = 2.170
    // Mantissa = 2, Fraction = 0.170 × 16 ≈ 3 → BRR = (2<<4)|3 = 0x23
    // Actual baud = 16MHz / (16 × 2.1875) = 457143 (~0.8% error, OK)
    USART2_BRR = 0x23;
    USART2_CR1 = (1 << 13) | (1 << 3);     // UE + TE

    // --- Configure ADC1 ---
    ADC1_CR1 = 0;
    ADC1_CR2 = 0;
    ADC1_SMPR2 = (3 << 0);                 // 56 cycles sample time
    ADC1_SQR3 = 0;                          // channel 0
    ADC1_CR2 |= (1 << 0);                  // ADON
    for (volatile int i = 0; i < 1000; i++);

    // --- Configure TIM2 for 16 kHz ---
    TIM2_PSC = 0;
    TIM2_ARR = 999;                         // 16MHz / 1000 = 16kHz
    TIM2_DIER |= (1 << 0);                 // update interrupt enable
    NVIC_ISER0 |= (1 << 28);               // enable TIM2 in NVIC
    TIM2_CR1 |= (1 << 0);                  // start timer

    uart_send_string("Polling mode. Streaming 5s every 10s.\r\n");

    while (1) {
        // Wait 10 seconds
        tick_counter = 0;
        while (tick_counter < POLL_TICKS);

        // Send header with known sample count, then stream
        uart_send_string("REC\r\n");
        uart_send_string("AUDIO:");
        uart_send_decimal(RECORD_SAMPLES);
        uart_send_string("\r\n");

        // Start recording into ring buffer
        ring_write = 0;
        sample_count = 0;
        uint32_t ring_read = 0;
        recording = 1;

        // Stream samples as they arrive — UART (46KB/s) outruns ADC (32KB/s)
        while (ring_read < RECORD_SAMPLES) {
            if (ring_read < ring_write) {
                uint16_t s = ring_buffer[ring_read & RING_MASK];
                uart_send_byte(s & 0xFF);
                uart_send_byte((s >> 8) & 0xFF);
                ring_read++;
            }
        }
        recording = 0;

        uart_send_string("DONE\r\n");
    }
}
