/*
 * Teensy OLED demo program.
 *
 * Works with Teensy 2.0 connected to an SSD1780-driven 128x32 display
 * - specifically a "Geekcreit 0.91 Inch 128x32 IIC I2C Blue OLED LCD
 * Display DIY Module"
 *
 * (C) 2012 Simon Frankau
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "usb_debug_only.h"
#include "print.h"


#define LED_ON          (PORTD |= (1<<6))
#define LED_OFF         (PORTD &= ~(1<<6))
#define LED_CONFIG      (DDRD |= (1<<6))

#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))
#define CPU_16MHz       0x00
#define CPU_8MHz        0x01
#define CPU_4MHz        0x02
#define CPU_2MHz        0x03
#define CPU_1MHz        0x04
#define CPU_500kHz      0x05
#define CPU_250kHz      0x06
#define CPU_125kHz      0x07
#define CPU_62kHz       0x08

// I2C on D0/D1: SCL on D0, SDA on D1
//
// In I2C the lines float high and are actively pulled low, so we set
// them to output zero, and enable/disable driving it low.
#define SCL_CONFIG (DDRD &= ~(1 << 0), PORTD &= ~(1 << 0))
#define SDA_CONFIG (DDRD &= ~(1 << 1), PORTD &= ~(1 << 1))

#define SCL_HIGH (DDRD &= ~(1 << 0))
#define SCL_LOW  (DDRD |= (1 << 0))
#define SCL_READ (PIND & (1 << 0))

#define SDA_HIGH (DDRD &= ~(1 << 1))
#define SDA_LOW  (DDRD |= (1 << 1))
#define SDA_READ (PIND & (1 << 1))

#define I2C_READ 1
#define I2C_WRITE 0

static const char i2c_init_instrs[] = {
    // Data sheet recommended initialisation sequence:

    // Set mux
    0x00, 0xa8, 0x1f, // Only 32 rows
    // Set display offset
    0x00, 0xd3, 0x00,
    // Set display start line
    0x00, 0x40,
    // Set segment remap
    0x00, 0xa0,
    // Set COM scan direction
    0x00, 0xc0,
    // Set COM pin hw conf
    0x00, 0xda, 0x02, // This display only uses alternate COM lines.
    // Contrast control
    0x00, 0x81, 0x7f,
    // Disable entire display on
    0x00, 0xa4,
    // Set normal display
    0x00, 0xa6,
    // Set oscillator frequency
    0x00, 0xd5, 0x80,
    // Enable charge pump regulator
    0x00, 0x08d, 0x14,
    // Turn display on.
    0x00, 0xaf,

    // Turn on all pixels.
    0x00, 0xa5,
};

static const int i2c_init_instrs_len =
    sizeof(i2c_init_instrs) / sizeof(*i2c_init_instrs);

static void i2c_init(void)
{
    SCL_CONFIG;
    SDA_CONFIG;
}

// Cycles the clock high then low again. May wait for a receiver
// holding the clock down for clock stretching.
static void i2c_clock(void)
{
    SCL_HIGH;
    // Receiver may be holding clock down to clock stretch...
    while (SCL_READ == 0) {
    }
    SCL_LOW;
}

// TODO: Currently works because I've set the clock slow. We may need delays.
static void i2c_send_bit(int i)
{
    // Set data up first...
    if (i) {
        SDA_HIGH;
    } else {
        SDA_LOW;
    }
    // then cycle the clock.
    i2c_clock();
}

static void i2c_send_byte(char c)
{
    for (char mask = 0x80; mask != 0; mask >>= 1) {
        i2c_send_bit(c & mask);
    }

    // Ack bit is sent by device. Don't drive SDA during this.
    SDA_HIGH;
    int acked = !SDA_READ;
    // And ack the ack/nack with a normal clock cycle.
    i2c_clock();

    // TODO: Debug info...
    if (acked) {
        print("ACK\n");
    } else {
        print ("NACK\n");
    }
}

static void oled_init(void)
{
    // Initiate request with a start condition.
    SDA_LOW;
    SCL_LOW;

    // Start I2C request with the display address.
    // TODO: Could be the other addr?
    i2c_send_byte(0x78);

    for (int i = 0; i < i2c_init_instrs_len; i++) {
        i2c_send_byte(i2c_init_instrs[i]);
    }

    // Stop condition.
    SCL_HIGH;
    SDA_HIGH;
}

int main(void)
{
    // CPU prescale must be set with interrupts disabled. They're off
    // when the CPU starts.
    //
    // Don't forget to sync this with F_CPU in the Makefile.
    CPU_PRESCALE(CPU_250kHz);
    LED_CONFIG;
    LED_OFF;
    i2c_init();

    // Initialise USB for debug, but don't wait.
    usb_init();

    // Blink and print. \o/
    while (1) {
        LED_ON;
        _delay_ms(500);
        LED_OFF;
        _delay_ms(500);
        print("Hello, world\n");
        oled_init();
    }
}
