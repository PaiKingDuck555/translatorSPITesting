/**
 ******************************************************************************
 * @file           : main_bridge.c
 * @brief          : Simple UART→SPI bridge. Receives text from Mac over UART,
 *                   forwards it to RPi over SPI1 (slave mode).
 *
 *                   UART: PA2=TX, PA3=RX (USART2, 115200 baud)
 *                   SPI:  PA5=SCK, PA6=MISO, PA7=MOSI (SPI1 slave, Mode 0)
 *
 *                   Debug GPIOs (mirror SPI1_SR flags for Saleae):
 *                     PB3  = OVR  (overrun,  SR bit 6)
 *                     PB4  = RXNE (rx ready, SR bit 0)
 *                     PB5  = TXE  (tx empty, SR bit 1)
 *                     PB10 = BSY  (busy,     SR bit 7)
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

// GPIOB (debug outputs)
#define GPIOB_MODER   (*(volatile uint32_t *)0x40020400)
#define GPIOB_ODR     (*(volatile uint32_t *)0x40020414)

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

// Debug pin masks
#define DBG_OVR_PIN   (1 << 3)    // PB3
#define DBG_RXNE_PIN  (1 << 4)    // PB4
#define DBG_TXE_PIN   (1 << 5)    // PB5
#define DBG_BSY_PIN   (1 << 10)   // PB10
#define DBG_ALL_PINS  (DBG_OVR_PIN | DBG_RXNE_PIN | DBG_TXE_PIN | DBG_BSY_PIN)

// Message buffer
#define BUF_SIZE 256
static uint8_t msg_buffer[BUF_SIZE];
static uint32_t msg_len = 0;

// --- Debug: mirror SPI1_SR flags onto PB3/PB4/PB5/PB10 ---
static void debug_mirror_spi(void) {
    uint32_t sr = SPI1_SR;
    uint32_t odr = GPIOB_ODR;

    odr &= ~DBG_ALL_PINS;  // clear all debug pins

    if (sr & (1 << 6))  odr |= DBG_OVR_PIN;   // OVR  → PB3
    if (sr & (1 << 0))  odr |= DBG_RXNE_PIN;  // RXNE → PB4
    if (sr & (1 << 1))  odr |= DBG_TXE_PIN;   // TXE  → PB5
    if (sr & (1 << 7))  odr |= DBG_BSY_PIN;   // BSY  → PB10

    GPIOB_ODR = odr;
}

// --- UART helpers ---

static void uart_send_byte(uint8_t b) {
    while (!(USART2_SR & (1 << 7)));
    USART2_DR = b;
}

static void uart_send_string(const char *s) {
    while (*s) uart_send_byte(*s++);
}

// --- SPI helpers ---

// Keep SPI responding 0xFF whenever the master clocks
static void spi_service_idle(void) {
    debug_mirror_spi();

    if (SPI1_SR & (1 << 0)) {       // RXNE — master clocked a byte
        (void)SPI1_DR;               // clear RXNE
        debug_mirror_spi();
    }
    if (SPI1_SR & (1 << 6)) {       // OVR — overrun, clear it
        (void)SPI1_DR;
        (void)SPI1_SR;
        debug_mirror_spi();
    }
    if (SPI1_SR & (1 << 1)) {       // TXE — TX buffer empty, reload idle
        SPI1_DR = 0xFF;
        debug_mirror_spi();
    }
}

// Load a byte and wait for master to clock it out
static void spi_send_byte(uint8_t b) {
    while (!(SPI1_SR & (1 << 1))) {  // wait TXE
        debug_mirror_spi();
    }
    SPI1_DR = b;
    debug_mirror_spi();

    while (!(SPI1_SR & (1 << 0))) {  // wait RXNE (master clocked)
        debug_mirror_spi();
    }
    (void)SPI1_DR;                    // clear RXNE
    debug_mirror_spi();
}

int main(void) {
    // --- Enable clocks ---
    RCC_AHB1ENR |= (1 << 0) | (1 << 1);   // GPIOA + GPIOB
    RCC_APB1ENR |= (1 << 17);              // USART2
    RCC_APB2ENR |= (1 << 12);              // SPI1

    // --- Configure debug pins: PB3, PB4, PB5, PB10 as outputs ---
    GPIOB_MODER &= ~((3 << 6) | (3 << 8) | (3 << 10) | (3 << 20));
    GPIOB_MODER |=  ((1 << 6) | (1 << 8) | (1 << 10) | (1 << 20));
    GPIOB_ODR &= ~DBG_ALL_PINS;  // start low

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
    debug_mirror_spi();

    uart_send_string("READY\r\n");

    while (1) {
        // --- Step 1: Read UART message, while keeping SPI fed with 0xFF ---
        msg_len = 0;
        while (msg_len < BUF_SIZE - 1) {
            // Wait for next UART byte, servicing SPI idle in the meantime
            while (!(USART2_SR & (1 << 5))) {
                spi_service_idle();
            }
            uint8_t c = (uint8_t)USART2_DR;
            if (c == '\n') break;
            if (c == '\r') continue;
            msg_buffer[msg_len++] = c;
        }
        msg_buffer[msg_len] = 0;

        // Echo back to Mac
        uart_send_string("GOT:");
        uart_send_string((char *)msg_buffer);
        uart_send_string("\r\n");

        // --- Step 2: Send length + data over SPI ---
        spi_send_byte((uint8_t)msg_len);

        for (uint32_t i = 0; i < msg_len; i++) {
            spi_send_byte(msg_buffer[i]);
        }

        // Reload idle
        while (!(SPI1_SR & (1 << 1))) {
            debug_mirror_spi();
        }
        SPI1_DR = 0xFF;
        debug_mirror_spi();

        uart_send_string("SENT\r\n");
    }
}
