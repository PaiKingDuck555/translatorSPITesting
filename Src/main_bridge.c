/**
 ******************************************************************************
 * @file           : main_bridge.c
 * @brief          : Simple UART→SPI bridge. Receives text from Mac over UART,
 *                   forwards it to RPi over SPI1 (slave mode).
 *
 *                   UART: PA2=TX, PA3=RX (USART2, 115200 baud)
 *                   SPI:  PA5=SCK, PA6=MISO, PA7=MOSI (SPI1 slave, Mode 0)
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

// USART2
#define USART2_SR     (*(volatile uint32_t *)0x40004400)
#define USART2_DR     (*(volatile uint32_t *)0x40004404)
#define USART2_BRR    (*(volatile uint32_t *)0x40004408)
#define USART2_CR1    (*(volatile uint32_t *)0x4000440C)

// SPI1
#define SPI1_CR1      (*(volatile uint32_t *)0x40013000)
#define SPI1_CR2      (*(volatile uint32_t *)0x40013004)
#define SPI1_SR       (*(volatile uint32_t *)0x40013008)
#define SPI1_DR       (*(volatile uint32_t *)0x4001300C)

// Message buffer — small, just for text
#define BUF_SIZE 256
static uint8_t msg_buffer[BUF_SIZE];
static uint32_t msg_len = 0;

// --- UART helpers ---

static void uart_send_byte(uint8_t b) {
    while (!(USART2_SR & (1 << 7)));
    USART2_DR = b;
}

static void uart_send_string(const char *s) {
    while (*s) uart_send_byte(*s++);
}

static uint8_t uart_recv_byte(void) {
    while (!(USART2_SR & (1 << 5)));  // wait RXNE
    return (uint8_t)USART2_DR;
}

// --- SPI helper ---

static void spi_load_and_wait(uint8_t b) {
    while (!(SPI1_SR & (1 << 1)));  // wait TXE
    SPI1_DR = b;
    while (!(SPI1_SR & (1 << 0)));  // wait RXNE (master clocked it)
    (void)SPI1_DR;                   // clear RXNE
}

int main(void) {
    // --- Enable clocks ---
    RCC_AHB1ENR |= (1 << 0);              // GPIOA
    RCC_APB1ENR |= (1 << 17);             // USART2
    RCC_APB2ENR |= (1 << 12);             // SPI1

    // --- PA2 = USART2 TX (AF7) ---
    GPIOA_MODER &= ~(3 << 4);
    GPIOA_MODER |= (2 << 4);
    GPIOA_AFRL &= ~(0xF << 8);
    GPIOA_AFRL |= (7 << 8);

    // --- PA3 = USART2 RX (AF7) ---
    GPIOA_MODER &= ~(3 << 6);
    GPIOA_MODER |= (2 << 6);
    GPIOA_AFRL &= ~(0xF << 12);
    GPIOA_AFRL |= (7 << 12);

    // --- Configure USART2: 115200 baud, TX + RX ---
    USART2_BRR = 0x8B;
    USART2_CR1 = (1 << 13) | (1 << 3) | (1 << 2);  // UE + TE + RE

    // --- PA5=SCK, PA6=MISO, PA7=MOSI → AF5 (SPI1) ---
    GPIOA_MODER &= ~((3 << 10) | (3 << 12) | (3 << 14));
    GPIOA_MODER |=  ((2 << 10) | (2 << 12) | (2 << 14));
    GPIOA_AFRL &= ~((0xF << 20) | (0xF << 24) | (0xF << 28));
    GPIOA_AFRL |=  ((5 << 20) | (5 << 24) | (5 << 28));

    // --- Configure SPI1: slave, 8-bit, Mode 0, software NSS ---
    SPI1_CR1 = 0;
    SPI1_CR2 = 0;
    SPI1_CR1 |= (1 << 9);   // SSM = 1
    SPI1_CR1 |= (1 << 6);   // SPE = 1

    // Pre-load idle byte
    SPI1_DR = 0xFF;

    uart_send_string("READY\r\n");

    while (1) {
        // --- Step 1: Read message from UART until newline ---
        msg_len = 0;
        while (msg_len < BUF_SIZE - 1) {
            uint8_t c = uart_recv_byte();
            if (c == '\n') break;
            if (c == '\r') continue;
            msg_buffer[msg_len++] = c;
        }
        msg_buffer[msg_len] = 0;  // null terminate

        // Echo back to Mac so we know it was received
        uart_send_string("GOT:");
        uart_send_string((char *)msg_buffer);
        uart_send_string("\r\n");

        // --- Step 2: Send over SPI to RPi ---
        // First: length byte
        spi_load_and_wait((uint8_t)msg_len);

        // Then: message bytes
        for (uint32_t i = 0; i < msg_len; i++) {
            spi_load_and_wait(msg_buffer[i]);
        }

        // Back to idle
        while (!(SPI1_SR & (1 << 1)));
        SPI1_DR = 0xFF;

        uart_send_string("SENT\r\n");
    }
}
