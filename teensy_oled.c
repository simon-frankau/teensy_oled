/*
 * Teensy OLED demo program.
 *
 * Works with Teensy 2.0 connected to an SSD1780-driven 128x32 display
 * - specifically a "Geekcreit 0.91 Inch 128x32 IIC I2C Blue OLED LCD
 * Display DIY Module"
 *
 * (C) 2021 Simon Frankau
 */

#include <stdlib.h>

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "cos_table.h"
#include "gen/charset.h"
#include "gen/head.h"
#include "gen/heels.h"
#include "usb_debug_only.h"
#include "print.h"

// Support the case where the OLED is configured for the alternate I2C
// address.
#ifdef ALTERNATIVE_OLED_ADDRESS
static const char OLED_SUB_ADDR = 2;
#else
static const char OLED_SUB_ADDR = 0;
#endif

// Suport rotating the display by 180 degrees.
#ifdef FLIPPED
    #define HFLIP 1
    #define VFLIP 8
#else
    #define HFLIP 0
    #define VFLIP 0
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
}

static inline char i2c_start(char addr)
{
    // An i2c transaction is initiated with an SDA transition while
    // SCL is high...
    i2c_pulldown(SDA);
    _delay_us(1);
    i2c_pulldown(SCL);

    return i2c_send_byte(addr);
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

////////////////////////////////////////////////////////////////////////
// OLED
//

// Sigh. For array initialisation, const values are insufficient...
#define OLED_ADDR                   (0x78 | OLED_SUB_ADDR)
#define OLED_CMD                    0x00
#define OLED_DATA                   0x40

#define OLED_SET_LOWER_COLUMN       0x00
#define OLED_SET_UPPER_COLUMN       0x10
#define OLED_SET_ADDR_MODE          0x20
#define OLED_SET_COL_ADDR           0x21
#define OLED_SET_PAGE_ADDR          0x22
#define OLED_SET_DISPLAY_START_LINE 0x40
#define OLED_SET_CONTRAST           0x81
#define OLED_SET_CHARGE_PUMP        0x8d
#define OLED_SET_SEGMENT_REMAP      0xa0
#define OLED_SET_ENTIRE_DISPLAY     0xa4
#define OLED_SET_INVERTED           0xa6
#define OLED_SET_MUX_RATIO          0xa8
#define OLED_SET_DISPLAY_ON_OFF     0xae
#define OLED_SET_PAGE_START_ADDR    0xb0
#define OLED_SET_COM_SCAN_DIR       0xc0
#define OLED_SET_DISPLAY_OFFSET     0xd3
#define OLED_SET_OSC_FREQ           0xd5
#define OLED_SET_COM_HW_CONF        0xda

static const char oled_init_instrs[] = {
    // Data sheet recommended initialisation sequence:
    OLED_CMD,
    // Set mux
    OLED_SET_MUX_RATIO, 0x1f, // Only 32 rows
    // Set display offset
    OLED_SET_DISPLAY_OFFSET, 0x00,
    // Set display start line
    OLED_SET_DISPLAY_START_LINE + 0x00,
    // Set segment remap
    OLED_SET_SEGMENT_REMAP | HFLIP,
    // Set COM scan direction
    OLED_SET_COM_SCAN_DIR | VFLIP,
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
};

static const int oled_init_instrs_len =
    sizeof(oled_init_instrs) / sizeof(*oled_init_instrs);

// Instructions to blit over entire screen
static const char oled_full_screen_instrs[] = {
    // Commands follow
    OLED_CMD,
    // Horizontal addressing mode.
    OLED_SET_ADDR_MODE, 0x00,
    // Columns 0x00 to 0x7f
    OLED_SET_COL_ADDR, 0x00, 0x7f,
    // Pages 0x00 to 0x07
    OLED_SET_PAGE_ADDR, 0x00, 0x07,
};

static const int oled_full_screen_instrs_len =
    sizeof(oled_full_screen_instrs) / sizeof(*oled_full_screen_instrs);

// Send a sequence of bytes over i2c.
static char oled_sequence(char const *data, int count)
{
    if (!i2c_start(OLED_ADDR)) {
        return 0;
    }
    int i;
    for (i = 0; i < count; i++) {
        if (!i2c_send_byte(data[i])) {
            break;
        }
    }
    i2c_stop();
    return i == count;
}

static char oled_init(void)
{
    return oled_sequence(oled_init_instrs, oled_init_instrs_len);
}

// I'm lazy, so I'm going to assume that once we have successfully
// initialised the display, all other operations succeed. No error
// checking on the remaining functions.

static void oled_clear(void)
{
    // Prepare to blit over the entire screen.
    oled_sequence(oled_full_screen_instrs, oled_full_screen_instrs_len);

    // And write the data
    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_DATA);
    for (int i = 0; i < 128 * 4; i++) {
        i2c_send_byte(0x00);
    }
    i2c_stop();
}

// Set page mode, and initial page (y*8) and x coordinate.
static void oled_set_page_mode(char page, char x) {
    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_CMD);
    i2c_send_byte(OLED_SET_ADDR_MODE); i2c_send_byte(0x02); // Page mode
    i2c_send_byte(OLED_SET_PAGE_START_ADDR | page);
    // High nibble must be loaded first, else it zeros the low nibble.
    i2c_send_byte(OLED_SET_UPPER_COLUMN | (x >> 4));
    i2c_send_byte(OLED_SET_LOWER_COLUMN | (x & 0x0f));
    i2c_stop();
}

// Blit an image to the screen. Y coordinates are pages (multiples of 8 pixels)
static void oled_blit(char x, char y, char w, char h, char const *image)
{
    // I'd much rather use horizontal addressing mode, but when we set
    // the start and end column it acutally starts loading memory at start
    // column & 0xf0. It wraps around to the right place, though. The
    // bugs of cheap hardware still surprise me.
    //
    // As it is, we use page mode, and write each page separately.
    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_CMD);
    i2c_send_byte(OLED_SET_ADDR_MODE); i2c_send_byte(0x02); // Page mode
    i2c_stop();

    char const *image_ptr = image;
    for (int page = y; page < y + h; page++) {
        oled_set_page_mode(page, x);

        i2c_start(OLED_ADDR);
        i2c_send_byte(OLED_DATA);
        for (int i = 0; i < w; i++) {
            i2c_send_byte(*image_ptr++);
        }
        i2c_stop();
    }
}

// Displays a string using the ZX Spectrum character set.
static void oled_write(char x, char y, char const *str)
{
    oled_set_page_mode(y, x);

    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_DATA);
    for (; *str != '\0'; str++) {
        char c = *str;
        char idx = (32 <= c && c < 128) ? c - 32 : 3;
        char const *ptr = charset + idx * 8;
        for (int i = 8; i > 0; i--) {
            i2c_send_byte(*ptr++);
        }
    }
    i2c_stop();
}

// Displays a string with a scrolling marquee effect.
// "speed" can be up to 8. "offset" is updated as it scrolls.
static void oled_marquee(char x, char y, char w,
                         char const *str, int *offset, int speed)
{
    oled_set_page_mode(y, x);

    char sub_offset = *offset & 0x07;

    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_DATA);

    char const *str_ptr = str + (*offset >> 3);
    while (w != 0) {
        char c = *str_ptr;
        char idx = (32 <= c && c < 128) ? c - 32 : 3;
        char const *ptr = charset + idx * 8 + sub_offset;
        for (int i = 8 - sub_offset; i > 0; i--) {
            i2c_send_byte(*ptr++);
            if (--w == 0) {
                break;
            }
        }
        sub_offset = 0;
        if (*++str_ptr == '\0') {
            str_ptr = str;
        }
    }
    i2c_stop();

    // Move the pointer along, returning to the start once we hit the end.
    *offset += speed;
    if (str[*offset >> 3] == '\0') {
        *offset &= 7;
    }
}

static void oled_bungee_marquee_aux(char const *str, int offset, int w)
{
    // We increase the scaling factor before the midpoint, decrease it after.
    int midpoint = (w >> 1);
    int scale = 0;

    char const *str_ptr = str + (offset >> 3);
    char sub_offset = offset & 0x07;

    // Run over the characters in the message loop.
    while (1) {
        char c = *str_ptr;
        char idx = (32 <= c && c < 128) ? c - 32 : 3;
        char const *ptr = charset + idx * 8 + sub_offset;
        // For each slice of the character displayed...
        for (int i = 8 - sub_offset; i > 0; i--) {
            // The factor of 8 empirically makes a nice effect on a 128 display.
            for (char j = 0; j < 1 + (scale / 8); j++) {
                i2c_send_byte(*ptr);
                if (--w == 0) {
                    return;
                }
            }
            ptr++;

            // Scaling code. The check is because the count up and
            // down is a bit uneven and can end up below 0 otherwise.
            scale += (w > midpoint) ? 1 : -1;
            if (scale < 0) {
                scale = 0;
            }
        }
        sub_offset = 0;
        if (*++str_ptr == '\0') {
            str_ptr = str;
        }
    }
}

// Like a marquee, but with characters of varying width.
static void oled_bungee_marquee(char x, char y, char w,
                                char const *str, int *offset)
{
    oled_set_page_mode(y, x);

    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_DATA);
    oled_bungee_marquee_aux(str, *offset, w);
    i2c_stop();

    // Move the pointer along, returning to the start once we hit the end.
    (*offset)++;
    if (str[*offset >> 3] == '\0') {
        *offset &= 7;
    }
}

// Like write, but with vertical wobble.
static void oled_wobble(char x, char y, char const *str, char *phase)
{
    {
        oled_set_page_mode(y, x);
        char shift = *phase;
        unsigned char const *str_ptr = (unsigned char const *)str;
        i2c_start(OLED_ADDR);
        i2c_send_byte(OLED_DATA);
        for (; *str_ptr != '\0'; str_ptr++) {
            char c = *str_ptr;
            char idx = (32 <= c && c < 128) ? c - 32 : 3;
            char const *ptr = charset + idx * 8;
            for (int i = 8; i > 0; i--) {
                char offset = cos_table_64_4[shift++ & 0x3f];
                i2c_send_byte(*ptr++ << offset);
            }
        }
        i2c_stop();
    }

    {
        oled_set_page_mode(y + 1, x);
        char shift = *phase;
        unsigned char const *str_ptr = (unsigned char const *)str;
        i2c_start(OLED_ADDR);
        i2c_send_byte(OLED_DATA);
        for (; *str_ptr != '\0'; str_ptr++) {
            char c = *str_ptr;
            char idx = (32 <= c && c < 128) ? c - 32 : 3;
            char const *ptr = charset + idx * 8;
            for (int i = 8; i > 0; i--) {
                char offset = 8 - cos_table_64_4[shift++ & 0x3f];
                i2c_send_byte(*ptr++ >> offset);
            }
        }
        i2c_stop();
    }

    (*phase)++;
}


static void oled_contrast(unsigned char c)
{
    i2c_start(OLED_ADDR);
    i2c_send_byte(OLED_CMD);
    i2c_send_byte(OLED_SET_CONTRAST);
    i2c_send_byte(c);
    i2c_stop();
}

////////////////////////////////////////////////////////////////////////
// And the main program itself...
//

char const message_1[] = "My little ssd1306+teensy 2.0 demo. ";
char const message_2[] = "Look... bendy text! :) ";
char const message_3[] = "Wobble!";

int main(void)
{
    // CPU prescale must be set with interrupts disabled. They're off
    // when the CPU starts.
    //
    // Don't forget to sync this with F_CPU in the Makefile.
    cpu_prescale(CPU_8MHz);
    led_init();
    led_off();
    i2c_init();

    // Initialise USB for debug, but don't wait.
    usb_init();

    // Wait for success init of the OLED.
    while (!oled_init()) {
        _delay_ms(20);
    }
    // And then do the initial drawing.
    oled_clear();
    oled_blit(0, 0, 24, 3, head);
    oled_blit(128 - 24, 0, 24, 3, heels);

    // Find the x coordinate to centre message_3:
    char m3_len = sizeof(message_3) - 1; // Remove NUL.
    char m3_x = (128 - 8 * m3_len) / 2;

    int offset1 = 0;
    int offset2 = 0;
    char phase = 0;

    int contrast = 0;

    while (1) {
        _delay_ms(20);

        // No idea if continually adjusting the contrast is good for
        // the hardware, but it's a nice effect.
#ifdef DO_CONTRAST
        contrast += 3;
        if (contrast> 100) {
            contrast -= 200;
        }
        oled_contrast(abs(contrast) + 30);
#endif // DO_CONTRAST

        oled_marquee(24, 2 , 128 - 24 - 24, message_1, &offset1, 2);
        oled_bungee_marquee(0, 3 , 128, message_2, &offset2);
        oled_wobble(m3_x, 0, message_3, &phase);
    }
}
