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

////////////////////////////////////////////////////////////////////////
// CPU prescaler
//

static const char CPU_16MHz  = 0x00;
static const char CPU_8MHz   = 0x01;
static const char CPU_4MHz   = 0x02;
static const char CPU_2MHz   = 0x03;
static const char CPU_1MHz   = 0x04;
static const char CPU_500kHz = 0x05;
static const char CPU_250kHz = 0x06;
static const char CPU_125kHz = 0x07;
static const char CPU_62kHz  = 0x08;

static inline void cpu_prescale(char i)
{
    CLKPR = 0x80;
    CLKPR = i;
}

////////////////////////////////////////////////////////////////////////
// LED
//

static inline void led_init(void)
{
    DDRD |= 1 << 6;
}

static inline void led_on(void)
{
    PORTD |= 1 << 6;
}

static inline void led_off(void)
{
    PORTD &= ~(1 << 6);
}

////////////////////////////////////////////////////////////////////////
// Low-level I2C config
//

// I2C on D0/D1: SCL on D0, SDA on D1
static const char SCL = 0;
static const char SDA = 1;

static void i2c_init(void)
{
    // In I2C the lines float high and are actively pulled low, so we
    // set them to output zero, and enable/disable driving it low.

    // SCL
    DDRD &= ~(1 << SCL);
    PORTD &= ~(1 << SCL);
    // SDA
    DDRD &= ~(1 << SDA);
    PORTD &= ~(1 << SDA);
}

static inline void i2c_release(char pin)
{
    DDRD &= ~(1 << pin);
}

static inline void i2c_pulldown(char pin)
{
    DDRD |= 1 << pin;
}

static inline char i2c_read(char pin)
{
    return PIND & (1 << pin);
}

static inline void i2c_start(void)
{
    // An i2c transaction is initiated with an SDA transition while
    // SCL is high..
    i2c_pulldown(SDA);
    i2c_pulldown(SCL);
}

static inline void i2c_stop(void)
{
    // And finishes with another SDA transition while SCL is high.
    i2c_release(SCL);
    i2c_release(SDA);
}

// Cycles the clock high then low again. May wait for a receiver
// holding the clock down for clock stretching.
static void i2c_clock(void)
{
    i2c_release(SCL);
    // Receiver may be holding clock down to clock stretch...
    while (i2c_read(SCL) == 0) {
    }
    i2c_pulldown(SCL);
}

// TODO: Currently works because I've set the clock slow. We may need delays.
static void i2c_send_bit(int i)
{
    // Set data up first...
    if (i) {
        i2c_release(SDA);
    } else {
        i2c_pulldown(SDA);
    }
    // then cycle the clock.
    i2c_clock();
}

static void i2c_send_byte(char c)
{
    // Send a byte of data.
    for (char mask = 0x80; mask != 0; mask >>= 1) {
        i2c_send_bit(c & mask);
    }

    // In reply, an ack bit is sent by device. Don't drive SDA during this.
    i2c_release(SDA);
    int acked = !i2c_read(SDA);
    // And ack the ack/nack with a normal clock cycle.
    i2c_clock();

    // TODO: Debug info...
    if (acked) {
        print("ACK\n");
    } else {
        print ("NACK\n");
    }
}

////////////////////////////////////////////////////////////////////////
// OLED
//

static const char oled_init_instrs[] = {
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

static const int oled_init_instrs_len =
    sizeof(oled_init_instrs) / sizeof(*oled_init_instrs);

static void oled_init(void)
{
    i2c_start();

    // Start I2C request with the display address.
    // TODO: Could be the other addr?
    i2c_send_byte(0x78);

    for (int i = 0; i < oled_init_instrs_len; i++) {
        i2c_send_byte(oled_init_instrs[i]);
    }

    i2c_stop();
}

int main(void)
{
    // CPU prescale must be set with interrupts disabled. They're off
    // when the CPU starts.
    //
    // Don't forget to sync this with F_CPU in the Makefile.
    cpu_prescale(CPU_250kHz);
    led_init();
    led_off();
    i2c_init();

    // Initialise USB for debug, but don't wait.
    usb_init();

    // Blink and print. \o/
    while (1) {
        led_on();
        _delay_ms(500);
        led_off();
        _delay_ms(500);
        print("Hello, world\n");
        oled_init();
    }
}
