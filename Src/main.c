/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Record audio from MAX9814 mic via ADC, dump over UART
 *                   Press blue button to record, release to send over serial.
 *                   No SPI, no Pi — just STM32 → Mac over USB serial.
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

// Audio buffer in RAM — 128KB total, use ~60KB for audio
// 30000 samples × 2 bytes = 60000 bytes = ~1.9 seconds at 16 kHz
#define MAX_SAMPLES 30000
static volatile uint16_t audio_buffer[MAX_SAMPLES];
static volatile uint32_t sample_count = 0;
static volatile uint8_t recording = 0;

// TIM2 ISR — called at 16 kHz
void TIM2_IRQHandler(void) {
    TIM2_SR = 0;  // clear flag

    if (!recording) return;
    if (sample_count >= MAX_SAMPLES) return;  // buffer full

    ADC1_CR2 |= (1 << 30);              // start conversion
    while (!(ADC1_SR & (1 << 1)));       // wait EOC
    audio_buffer[sample_count++] = ADC1_DR & 0xFFF;
}

static void uart_send_byte(uint8_t b) {
    while (!(USART2_SR & (1 << 7)));     // wait TXE
    USART2_DR = b;
}

static void uart_send_string(const char *s) {
    while (*s) uart_send_byte(*s++);
}

int main(void) {
    // --- Enable clocks ---
    RCC_AHB1ENR |= (1 << 0) | (1 << 2);   // GPIOA + GPIOC
    RCC_APB1ENR |= (1 << 0) | (1 << 17);  // TIM2 + USART2
    RCC_APB2ENR |= (1 << 8);               // ADC1

    // --- PA0 = analog (mic) ---
    GPIOA_MODER |= (3 << 0);

    // --- PA2 = USART2 TX (AF7) ---
    GPIOA_MODER &= ~(3 << 4);
    GPIOA_MODER |= (2 << 4);               // PA2 = alternate function
    GPIOA_AFRL &= ~(0xF << 8);
    GPIOA_AFRL |= (7 << 8);                // AF7 = USART2

    // --- Configure USART2: 115200 baud ---
    // APB1 = 16 MHz, BRR = 16000000 / 115200 ≈ 139 = 0x8B
    USART2_BRR = 0x8B;
    USART2_CR1 = (1 << 13) | (1 << 3);     // UE + TE (enable UART, enable TX)

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

    // --- Button debounce ---
    uint8_t button_stable = 0;
    uint32_t debounce_count = 0;

    uart_send_string("Ready. Press blue button to record.\r\n");

    while (1) {
        // Debounce
        uint8_t raw = !(GPIOC_IDR & (1 << 13));
        if (raw == button_stable) {
            debounce_count = 0;
        } else {
            debounce_count++;
            if (debounce_count >= 50000) {
                button_stable = raw;
                debounce_count = 0;

                if (button_stable) {
                    // Button pressed — start recording
                    sample_count = 0;
                    recording = 1;
                    uart_send_string("Recording...\r\n");
                } else {
                    // Button released — stop and dump audio
                    recording = 0;

                    // Send header: "AUDIO:<sample_count>\n"
                    uart_send_string("AUDIO:");
                    // Send sample count as decimal string
                    char num[12];
                    int n = sample_count;
                    int len = 0;
                    if (n == 0) num[len++] = '0';
                    else {
                        char tmp[12];
                        int tlen = 0;
                        while (n > 0) { tmp[tlen++] = '0' + (n % 10); n /= 10; }
                        for (int i = tlen - 1; i >= 0; i--) num[len++] = tmp[i];
                    }
                    num[len] = 0;
                    uart_send_string(num);
                    uart_send_string("\r\n");

                    // Send raw 12-bit samples as 2 bytes each (little-endian)
                    for (uint32_t i = 0; i < sample_count; i++) {
                        uint16_t s = audio_buffer[i];
                        uart_send_byte(s & 0xFF);         // low byte
                        uart_send_byte((s >> 8) & 0xFF);  // high byte
                    }

                    uart_send_string("DONE\r\n");
                }
            }
        }
    }
}
