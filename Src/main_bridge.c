/**
 ******************************************************************************
 * @file           : main_bridge.c
 * @brief          : UART-to-SPI bridge. Receives WAV file from Mac over UART,
 *                   streams it to RPi over SPI1 (slave mode).
 *
 *                   Protocol:
 *                   - Mac sends: SEND:<filesize>\r\n + raw bytes
 *                   - STM32 streams to SPI: sync(AA 55 AA 55) + size(4B) + data
 *                   - Idle SPI response: 0xFF
 *
 *                   UART: PA2=TX, PA3=RX (USART2, 460800 baud)
 *                   SPI:  PA5=SCK, PA6=MISO, PA7=MOSI (SPI1 slave, 1MHz, Mode 0)
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

// Ring buffer — UART fills, SPI drains
#define RING_SIZE 4096
#define RING_MASK (RING_SIZE - 1)
static volatile uint8_t ring_buffer[RING_SIZE];
static volatile uint32_t ring_write = 0;
static uint32_t ring_read = 0;

// --- UART helpers ---

static void uart_send_byte(uint8_t b) {
    while (!(USART2_SR & (1 << 7)));  // wait TXE
    USART2_DR = b;
}

static void uart_send_string(const char *s) {
    while (*s) uart_send_byte(*s++);
}

static uint8_t uart_recv_byte(void) {
    while (!(USART2_SR & (1 << 5)));  // wait RXNE
    return (uint8_t)USART2_DR;
}

// Read a line from UART into buf (up to maxlen-1 chars), null-terminate.
// Returns length. Stops at \n.
static int uart_read_line(char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        uint8_t c = uart_recv_byte();
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

// --- SPI helpers ---

// Wait for RPi to clock one byte out, send our byte
static void spi_send_byte(uint8_t b) {
    // Wait for TXE (TX buffer empty)
    while (!(SPI1_SR & (1 << 1)));
    SPI1_DR = b;
    // Wait for RXNE (transfer complete — master clocked it)
    while (!(SPI1_SR & (1 << 0)));
    (void)SPI1_DR;  // read to clear RXNE
}

// --- String helpers ---

// Parse decimal number from string. Returns 0 on failure.
static uint32_t parse_uint(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

// Check if str starts with prefix. Returns pointer past prefix, or 0.
static const char *starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return str;
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

    // --- Configure USART2: 460800 baud, TX + RX ---
    USART2_BRR = 0x23;
    USART2_CR1 = (1 << 13) | (1 << 3) | (1 << 2);  // UE + TE + RE

    // --- PA5 = SPI1_SCK (AF5), PA6 = SPI1_MISO (AF5), PA7 = SPI1_MOSI (AF5) ---
    GPIOA_MODER &= ~((3 << 10) | (3 << 12) | (3 << 14));
    GPIOA_MODER |=  ((2 << 10) | (2 << 12) | (2 << 14));
    GPIOA_AFRL &= ~((0xF << 20) | (0xF << 24) | (0xF << 28));
    GPIOA_AFRL |=  ((5 << 20) | (5 << 24) | (5 << 28));

    // --- Configure SPI1: slave, 8-bit, Mode 0, software NSS ---
    SPI1_CR1 = 0;
    SPI1_CR2 = 0;
    SPI1_CR1 |= (1 << 9);   // SSM = 1 (software slave management)
                              // SSI = 0 (bit 8, slave selected)
                              // MSTR = 0 (slave mode)
                              // CPOL = 0, CPHA = 0 (Mode 0)
                              // DFF = 0 (8-bit)
    SPI1_CR1 |= (1 << 6);   // SPE = 1 (enable SPI)

    // Pre-load idle byte
    SPI1_DR = 0xFF;

    uart_send_string("Bridge ready. Waiting for SEND command.\r\n");

    char line[64];

    while (1) {
        // --- Phase 1: Wait for SEND:<size> from Mac ---
        uart_read_line(line, sizeof(line));

        const char *after = starts_with(line, "SEND:");
        if (!after) {
            uart_send_string("ERR:expected SEND:<size>\r\n");
            continue;
        }

        uint32_t file_size = parse_uint(after);
        if (file_size == 0) {
            uart_send_string("ERR:bad size\r\n");
            continue;
        }

        uart_send_string("ACK\r\n");

        // --- Phase 2: Send sync marker over SPI ---
        spi_send_byte(0xAA);
        spi_send_byte(0x55);
        spi_send_byte(0xAA);
        spi_send_byte(0x55);

        // --- Phase 3: Send file size (big-endian, 4 bytes) ---
        spi_send_byte((file_size >> 24) & 0xFF);
        spi_send_byte((file_size >> 16) & 0xFF);
        spi_send_byte((file_size >> 8) & 0xFF);
        spi_send_byte(file_size & 0xFF);

        // --- Phase 4: Stream UART → ring buffer → SPI ---
        ring_write = 0;
        ring_read = 0;
        uint32_t uart_received = 0;
        uint32_t spi_sent = 0;

        while (spi_sent < file_size) {
            // Fill ring buffer from UART (non-blocking check)
            if (uart_received < file_size && (USART2_SR & (1 << 5))) {
                ring_buffer[ring_write & RING_MASK] = (uint8_t)USART2_DR;
                ring_write++;
                uart_received++;
            }

            // Drain ring buffer to SPI
            if (ring_read < ring_write) {
                spi_send_byte(ring_buffer[ring_read & RING_MASK]);
                ring_read++;
                spi_sent++;
            }
        }

        // Re-load idle byte
        while (!(SPI1_SR & (1 << 1)));
        SPI1_DR = 0xFF;

        uart_send_string("OK\r\n");
    }
}
