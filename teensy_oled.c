/*
 * Teensy OLED demo program.
 *
 * Works with Teensy 2.0 connected to an SSD1780-driven 128x32 display
 * - specifically a "Geekcreit 0.91 Inch 128x32 IIC I2C Blue OLED LCD
 * Display DIY Module"
 *
 * (C) 2021 Simon Frankau
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "gen/head.h"
#include "usb_debug_only.h"
#include "print.h"


#ifdef ALTERNATIVE_OLED_ADDRESS
static const char OLED_ADDR = 2;
#else
static const char OLED_ADDR = 0;
#endif

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

// Timing requirements:
//
// 2.5us per clock cycle
// 0.6us between SDA and SCL on start
// 0.6us between SCL and SDA on stop
// 1.3us idle time
// 0.1us data set-up
// 0.3us data hold
//
// Targeting 2MHz Teensy (default clock speed), so the main concern is anything that takes

static inline void i2c_start(void)
{
    // An i2c transaction is initiated with an SDA transition while
    // SCL is high...
    i2c_pulldown(SDA);
    _delay_us(1);
    i2c_pulldown(SCL);
}

static inline void i2c_stop(void)
{
    // And finishes with another SDA transition while SCL is high.
    i2c_pulldown(SDA); // Start with SDA down.
    _delay_us(1);
    i2c_release(SCL);
    _delay_us(1);
    i2c_release(SDA);
    // Idle time
    _delay_us(2);
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

static char i2c_send_byte(char c)
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

    return acked;

    if (!acked) {
        print ("I2C NACK :(\n");
    }
}

////////////////////////////////////////////////////////////////////////
// OLED
//

// Sigh. For array initialisation, const values are insufficient...
#define OLED_SET_MUX_RATIO          0xa8
#define OLED_SET_DISPLAY_OFFSET     0xd3
#define OLED_SET_DISPLAY_START_LINE 0x40
#define OLED_SET_SEGMENT_REMAP      0xa0
#define OLED_SET_COM_SCAN_DIR       0xc0
#define OLED_SET_COM_HW_CONF        0xda
#define OLED_SET_CONTRAST           0x81
#define OLED_SET_ENTIRE_DISPLAY     0xa4
#define OLED_SET_INVERTED           0xa6
#define OLED_SET_OSC_FREQ           0xd5
#define OLED_SET_CHARGE_PUMP        0x8d
#define OLED_SET_ADDR_MODE          0x20
#define OLED_SET_COL_ADDR           0x21
#define OLED_SET_PAGE_ADDR          0x22
#define OLED_SET_DISPLAY_ON_OFF     0xAE

static const char oled_init_instrs[] = {
    // Data sheet recommended initialisation sequence:

    // Start with 0x00 to signify that commands follow.
    0x00,

    // Set mux
    OLED_SET_MUX_RATIO, 0x1f, // Only 32 rows
    // Set display offset
    OLED_SET_DISPLAY_OFFSET, 0x00,
    // Set display start line
    OLED_SET_DISPLAY_START_LINE + 0x00,
    // Set segment remap
    OLED_SET_SEGMENT_REMAP | 0x00, // 0x01 for reverse mapping
    // Set COM scan direction
    OLED_SET_COM_SCAN_DIR | 0x00, // 0x08 for reverse mapping
    // Set COM pin hw conf
    OLED_SET_COM_HW_CONF, 0x02, // Alternate lines, normal direction
    // Contrast control
    OLED_SET_CONTRAST, 0x7f,
    // Disable entire display on
    OLED_SET_ENTIRE_DISPLAY | 0x00, // 0x01 to light entire display
    // Set normal display
    OLED_SET_INVERTED | 0x00, // 0x01 to invert
    // Set oscillator frequency
    OLED_SET_OSC_FREQ, 0x80,
    // Enable charge pump regulator
    OLED_SET_CHARGE_PUMP, 0x14, // 0x10 to disable.
    // Turn display on.
    OLED_SET_DISPLAY_ON_OFF | 0x01,

    // Extra instructions beyond datasheet.

    // Set horizontal addressing mode.
    OLED_SET_ADDR_MODE, 0x00,

    // TODO: Attach this to the source blit function.
    // Start and end columns
    OLED_SET_COL_ADDR, 0x00, 0x7f,
    // Start and end pages
    OLED_SET_PAGE_ADDR, 0x00, 0x07,
};

static const int oled_init_instrs_len =
    sizeof(oled_init_instrs) / sizeof(*oled_init_instrs);

static void oled_init(void)
{
    i2c_start();

    // Start I2C request with the display address.
    i2c_send_byte(0x78 | OLED_ADDR);

    for (int i = 0; i < oled_init_instrs_len; i++) {
        i2c_send_byte(oled_init_instrs[i]);
    }

    i2c_stop();
}

static void oled_pattern(void)
{
    i2c_start();

    // Start I2C request with the display address.
    i2c_send_byte(0x78 | OLED_ADDR);

    // Start writing data.
    i2c_send_byte(0x40);

    for (int i = 0; i < 128 * 4; i++) {
        i2c_send_byte(i);
        // i2c_send_byte(0xaa);
    }

    i2c_stop();

}

static void oled_pattern2(void)
{
    // TODO: Further experimenting suggests this is not the case.
    // Needs more investigation. <shruggie/>
    //
    // Looks to me like Co bit means do just one command, then have another check.
    // Useful for starting off with a command then switching to data, e.g. for
    // blitting.

    i2c_start();

    // Start I2C request with the display address.
    i2c_send_byte(0x78 | OLED_ADDR);

    // Start writing data.

    // TODO: Testing clipping to "display all on" then back to "show
    // memory", before filling data.
    i2c_send_byte(0x80);
    i2c_send_byte(0xa5);
    i2c_send_byte(0x80);
    i2c_send_byte(0xa4);
    i2c_send_byte(0x40);

    for (int i = 0; i < 64 * 4; i++) {
        i2c_send_byte(0xaa);
        i2c_send_byte(0x55);
    }

    i2c_stop();
}

static void oled_pattern3(void)
{
    // Looks to me like Co bit means do just one command, then have another check.
    // Useful for starting off with a command then switching to data, e.g. for
    // blitting.

    i2c_start();

    // Start I2C request with the display address.
    i2c_send_byte(0x78 | OLED_ADDR);

    i2c_send_byte(0x00);
    i2c_send_byte(0x21); i2c_send_byte(0x00); i2c_send_byte(0x17);

    i2c_stop();

    i2c_start();

    // Start I2C request with the display address.
    i2c_send_byte(0x78 | OLED_ADDR);


    // Start writing data.

    // TODO: Testing clipping to "display all on" then back to "show
    // memory", before filling data.
//    i2c_send_byte(0x80);
//    i2c_send_byte(0x21); i2c_send_byte(0x00); i2c_send_byte(0x18);
//    i2c_send_byte(0x80);
//    i2c_send_byte(0x22); i2c_send_byte(0x00); i2c_send_byte(0x02);
    i2c_send_byte(0x40);

    for (int i = 0; i < 24 * 3; i++) {
        i2c_send_byte(image[i]);
    }

    i2c_stop();
}



int main(void)
{
    // CPU prescale must be set with interrupts disabled. They're off
    // when the CPU starts.
    //
    // Don't forget to sync this with F_CPU in the Makefile.
    cpu_prescale(CPU_2MHz);
    led_init();
    led_off();
    i2c_init();

    // Initialise USB for debug, but don't wait.
    usb_init();

    int flip = 0;

    // Blink and print. \o/
    while (1) {
        led_on();
        _delay_ms(500);
        led_off();
        _delay_ms(500);
        oled_init();
        if (flip) {
            oled_pattern3();
        } else {
            oled_pattern();
        }
        flip = !flip;
    }
}
