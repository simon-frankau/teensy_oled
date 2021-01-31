/*
 * Teensy LCD demo program.
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

int main(void)
{
    // CPU prescale must be set with interrupts disabled. They're off
    // when the CPU starts.
    //
    // Don't forget to sync this with F_CPU in the Makefile.
    CPU_PRESCALE(CPU_250kHz);
    LED_CONFIG;
    LED_OFF;

    // Initialise USB for debug, but don't wait.
    usb_init();

    // Blink and print. \o/
    while (1) {
        LED_ON;
        _delay_ms(500);
        LED_OFF;
        _delay_ms(500);
        print("Hello, world\n");
    }
}
