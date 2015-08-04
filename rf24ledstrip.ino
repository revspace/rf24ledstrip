#include <avr/pgmspace.h>
#include <stdint.h>
#include <SPI.h>
#include <RF24.h>
#include "color.h"

#define DEBUG
#include "debug.h"


// memo
// echo 99669966004c454453... > /dev/ttyUSB0 

/*
perl -le'for(;;) { select undef, undef, undef, 1; $p = +("99669966004c45445300" . unpack("H*", join "", map { chr int rand 256 } 1..3)); for (1..10) { print $p; select undef, undef, undef, .01; } }'  > /dev/ttyUSB0
*/

const int flag_addr  =   1;  // Specify a recipient mask
const int flag_wait  =   2;  // Specify a wait time
const int flag_waitx =   4;  // Same, but multiplied by id
const int flag_flash =   8;  // Specify pattern: on time, off time, iterations
const int flag_fade  =  16;  // Specify a fade time
const int flag_meh1  =  32;
const int flag_meh2  =  64;
const int flag_more  = 128;  // More flags!

const int pin_led_r = 5;   // pwm
const int pin_led_g = 6;   // pwm
const int pin_led_b = 9;   // pwm

const int pin_id_1 = 2;
const int pin_id_2 = 3;
const int pin_id_4 = 4;
const int pin_id_8 = 7;

const int pin_rf_ce = 8;
const int pin_rf_cs = 10;

const float factor_r = 1;
const float factor_g = .65;
const float factor_b = .7;



const int factor_time = 64;  // compensation for faster timer

const uint32_t keepalive_interval = 20000 * factor_time;

static long int address = 0x66996699L;

static float fade_r;
static float fade_g;
static float fade_b;
static Color fade_from;
static Color fade_to;
static uint32_t fade_begin;
static uint32_t fade_end = 0;


uint8_t cielab[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4,
4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9,
9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15,
16, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24,
24, 25, 25, 26, 26, 27, 28, 28, 29, 29, 30, 31, 31, 32, 33, 33, 34, 35,
35, 36, 37, 37, 38, 39, 40, 40, 41, 42, 43, 44, 44, 45, 46, 47, 48, 49,
49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
67, 68, 69, 70, 71, 72, 73, 75, 76, 77, 78, 79, 80, 82, 83, 84, 85, 87,
88, 89, 90, 92, 93, 94, 96, 97, 99, 100, 101, 103, 104, 106, 107, 108,
110, 111, 113, 114, 116, 118, 119, 121, 122, 124, 125, 127, 129, 130,
132, 134, 135, 137, 139, 141, 142, 144, 146, 148, 149, 151, 153, 155,
157, 159, 161, 162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182,
185, 187, 189, 191, 193, 195, 197, 200, 202, 204, 206, 208, 211, 213,
215, 218, 220, 222, 225, 227, 230, 232, 234, 237, 239, 242, 244, 247,
249, 252, 255 };

int me;

RF24 rf(pin_rf_ce, pin_rf_cs);

void read_id() {
    me = digitalRead(pin_id_1) * 1
       + digitalRead(pin_id_2) * 2
       + digitalRead(pin_id_4) * 4
       + digitalRead(pin_id_8) * 8;
}

void mydelay(uint32_t ms) {
    delay(ms * factor_time);
}

void slurp() {
    char* dummy[65];
    while (rf.available()) rf.read(&dummy, sizeof dummy);
}

void led(Color color) {
    uint8_t r = color.r * factor_r;
    uint8_t g = color.g * factor_g;
    uint8_t b = color.b * factor_b;
    D("Setting color to #%02x%02x%02x", color.r, color.g, color.b);

    analogWrite(pin_led_r, cielab[r]);
    analogWrite(pin_led_g, cielab[g]);
    analogWrite(pin_led_b, cielab[b]);
}

void setup_radio() {
    rf.begin();

    if (! rf.isPVariant()) {
        Serial.println("No nRF24L01+. Halting.");
        for (;;);
    }
    rf.enableDynamicPayloads();
    rf.openReadingPipe(1, address);
    rf.startListening();
    D("setup_radio done; CRC=%d", rf.getCRCLength());
}

void setup () {

    bitSet(TCCR1B, WGM12);
    TCCR0B = TCCR0B & 0b11111000 | 0x01;
    TCCR1B = TCCR1B & 0b11111000 | 0x01;
    TCCR2B = TCCR2B & 0b11111000 | 0x01;

    pinMode(pin_led_r, OUTPUT);
    pinMode(pin_led_g, OUTPUT);
    pinMode(pin_led_b, OUTPUT);
    pinMode(pin_id_1, OUTPUT);
    pinMode(pin_id_2, OUTPUT);
    pinMode(pin_id_4, OUTPUT);
    pinMode(pin_id_8, OUTPUT);
    digitalWrite(pin_id_1, HIGH);
    digitalWrite(pin_id_2, HIGH);
    digitalWrite(pin_id_4, HIGH);
    digitalWrite(pin_id_8, HIGH);
    pinMode(13, OUTPUT);

    digitalWrite(13, HIGH);
    led(red);   mydelay(100);
    led(green); mydelay(100);
    led(blue);  mydelay(100);
    digitalWrite(13, LOW);
    led(off);

    read_id();
    Serial.begin(57600);
    debug_begin();
    D("Hello, my name is %d.", me);

    setup_radio();

    D("Setup done.");
}

char _waiting[65];
char* waiting = _waiting;
uint32_t wait_until = 0;

Color c;

void flash(int ontime, int offtime, int times, Color color) {
    D("Flashing %d times (%d ms on, %d ms off)", times, ontime, offtime);

    for (int i = 0; i < times; i++) {
        led(color);
        mydelay(ontime);
        led(off);
        mydelay(offtime);
    }

    led(c);
}

void handle_param(char* buf, bool waited = false) {
    char* buf_param = buf;
    uint16_t idmask = 0;

    uint8_t flash_on;
    uint8_t flash_off;
    uint8_t flash_times;

    D("Param: %s", buf);

    uint8_t flags = *((uint8_t*) buf++);

    D("Flags: %d", flags);

    if (flags & flag_addr) {
        idmask = *((uint16_t*) buf);
        buf += 2;

        if (! (idmask & (1 << me))) {
            D("Not for me.");
            return;
        }
    }

    if (waited) {
        D("Done waiting.");
        buf += 1;
    } else if (flags & flag_wait) {
        uint8_t wait = *((uint8_t*) buf++);

        int factor = flags & flag_waitx ? me : 1;
        wait *= factor;
        D("Waiting %d.%d seconds.", wait / 10, wait % 10);
        wait_until = millis() + (uint32_t) 100 * wait * factor_time;

        memcpy(waiting, buf_param, 32);
        return;
    }

    if (flags & flag_flash) {
        // After flashing, return to previous color.
        flash_on    = *((uint8_t*) buf++) * 10;;
        flash_off   = *((uint8_t*) buf++) * 10;;
        flash_times = *((uint8_t*) buf++);
    }

    if (flags & flag_fade) {
        uint8_t fade_time = *((uint8_t*) buf++);

        fade_begin = millis();
        fade_end = fade_begin + (uint32_t) 100 * fade_time * factor_time;
    }

    if (flags & flag_meh1) return;
    if (flags & flag_meh2) return;
    if (flags & flag_more) return;

    char* buf_color = buf;

    if (idmask) {
        for (int i = 0; i < 16; i++) {
            if (idmask & (1 << i)) {
                if (i == me) {
                    buf_color = buf;
                    break;
                }
                buf += 3;
            }
        }
    }

    if (flags & flag_fade) {
        fade_to = *((Color*) buf_color);
        fade_from = c;
        uint32_t fade_ticks = fade_end - fade_begin;
        fade_r = (float) (fade_to.r - fade_from.r) / fade_ticks;
        fade_g = (float) (fade_to.g - fade_from.g) / fade_ticks;
        fade_b = (float) (fade_to.b - fade_from.b) / fade_ticks;;

        Serial.println("FAAAAAAAADE!");
        Serial.println(fade_ticks);
        Serial.println(fade_r);
        Serial.println(fade_g);
        Serial.println(fade_b);
        D("#%02x%02x%02x -> %02x%02x%02x", fade_from.r, fade_from.g, fade_from.b,
                                         fade_to.r, fade_to.g, fade_to.b);

        return;
    }

    if (flags & flag_flash) {
        Color flash_color = *((Color*) buf_color);
        flash(flash_on, flash_off, flash_times, flash_color);

        slurp();
        return;
    }

    c = *((Color*) buf_color);
    led(c);
}


static uint32_t next_keepalive = keepalive_interval;

void loop () {
    static char _buf[65];
    char* buf = _buf;
    char* buf_param = buf + 5;

    mydelay(10);

    if (wait_until && millis() >= wait_until) {
        D("Done waiting.");
        handle_param(waiting, true);
        wait_until = 0;
    }

    if (!rf.available()) {
        if (!fade_end) return;

        // No new data. Let's fade!
        uint32_t now = millis();
        if (now >= fade_end) {
            D("aeou");
            led(c = fade_to);
            fade_end = 0;
        } else {
            uint32_t delta = now - fade_begin;

            c.r = fade_from.r + fade_r * delta;
            c.g = fade_from.g + fade_g * delta;
            c.b = fade_from.b + fade_b * delta;
            led(c);
        }


        return;
    }

    rf.read(&_buf, sizeof _buf);
    Serial.println(buf);
    buf++;  // ignore length byte.

    setup_radio();

    if (memcmp("DBEL", buf, 4) == 0) {
        flash(1000, 1000, 10, yellow);
        slurp();
        return;
    }

    if (memcmp("LEDS", buf, 4) != 0)
        return;

    handle_param(buf_param);

}

