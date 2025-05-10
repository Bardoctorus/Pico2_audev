#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"

// pio include
#include "i2s.pio.h"


//i2s pindefs
#define DATA_OUT 6
#define BCLK 7
#define WS 8 // LCLK is another name this goes by - basically L/R audio trigger

void dataForever(PIO pio, uint sm){
    while(1){
        pio_sm_put_blocking(pio, sm, 0xF0F0F0F0); // 32 bit on off on off
    }

}

void pio_i2s_init(PIO pio, uint sm, uint offset){

    //set up pins for PIO use
    pio_gpio_init(pio, DATA_OUT);
    pio_gpio_init(pio, BCLK);
    pio_gpio_init(pio, WS);
    //setting consecutive pins
    pio_sm_set_consecutive_pindirs(pio, sm, DATA_OUT, 1, true); // true = output
    pio_sm_set_consecutive_pindirs(pio, sm , BCLK, 2, true); // 2 pins, BLCK and WS, that's why pins 7 and 8, consecutive!

    // set up our config variable
    pio_sm_config c = i2s_program_get_default_config(offset);
    // tell pins what they are doing
    sm_config_set_out_pins(&c, DATA_OUT, 1);
    sm_config_set_sideset_pins(&c, BCLK); //includes WS as we set that up earlier (pio_sm_set_consecutive_pindirs)
    sm_config_set_out_shift(&c, false, true, 32); // shift OSR to left, true autopull, 32 bits
    sm_config_set_clkdiv(&c, 48.828125f); // correct value for 48k audio TODO: better cpu speed for div?

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_pins_with_mask(
        pio,
        sm,
        0,
        (1u << DATA_OUT) | (1u << BCLK) | (1u << WS)
    );

    pio_sm_exec(pio, sm, pio_encode_jmp(offset + i2s_offset_entry_point)); // points the program to start at our entry point.
}

int main()
{
    stdio_init_all();
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &i2s_program);

    pio_i2s_init(pio, sm, offset);
    pio_sm_set_enabled(pio,sm, true);
    dataForever(pio, sm);

    
}
