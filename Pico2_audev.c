#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <math.h>


// pio generated header file include
#include "i2s.pio.h"


//i2s pindefs
#define DATA_IN 9
#define DATA_OUT 6
#define BCLK 7
#define WS 8 // LCLK is another name this goes by - basically L/R audio trigger
#define SCK 15
// test sine wave defs
#define SAMPLE_RATE 48000.0f
#define TONE_HZ 440.0f
#define SCALE 8388607.0f
#define AUDIO_BUFFER_SIZE 256


int dma_out_ctrl;
int dma_out_data;

int dma_in_ctrl;
int dma_in_data;

uint32_t outputbuffer[AUDIO_BUFFER_SIZE*2];
uint32_t inputbuffer[AUDIO_BUFFER_SIZE*2];
__attribute__ ((aligned(8))) uint32_t * outputcontrol[2] = {outputbuffer, &outputbuffer[AUDIO_BUFFER_SIZE]};
__attribute__ ((aligned(8))) uint32_t * inputcontrol[2] = {inputbuffer, &inputbuffer[AUDIO_BUFFER_SIZE]};

float phase = 0.0f;
float step = 2.0f * (float)M_PI * TONE_HZ / SAMPLE_RATE;

void fillBufferSine(uint32_t * position){

    for (int i = 0 ; i < AUDIO_BUFFER_SIZE; i++){
        float s = sinf(phase) * 0.5f;
        // scale it to 24 bit value
        float scaled_val = s * SCALE;
        // truncate it to 24 bit int, 32 bit but at the LSB end
        int32_t scaled_val_int = (int32_t)scaled_val;
        // Pack into 32-bit MSBs
        uint32_t packed = ((uint32_t)scaled_val_int << 8);
        position[i] = packed;
        

        phase += step;
        if (phase > 2.0f * (float)M_PI)
            phase -= 2.0f * (float)M_PI;
    }
    

}
void dmahandler();


void dmasetup(PIO pio, int sm, int smin){

    // create 2 DMA channels
    dma_out_ctrl = dma_claim_unused_channel(true);
    dma_out_data = dma_claim_unused_channel(true);

    //configure the data channel
    dma_channel_config c = dma_channel_get_default_config(dma_out_ctrl);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_ring(&c, false, 3); // this means 64 bits before wrapping
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    dma_channel_configure(dma_out_ctrl,
    &c,
    &dma_hw->ch[dma_out_data].al3_read_addr_trig, //alias to read address trigger of data out dma
    outputcontrol, // pointer to a pointer yo
    1,
    false
    );
    dma_channel_config d = dma_channel_get_default_config(dma_out_data);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, true);
    channel_config_set_write_increment(&d, false);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&d, dma_out_ctrl);
    dma_channel_configure(dma_out_data,
    &d,
    &pio->txf[sm],
    NULL,
    AUDIO_BUFFER_SIZE,
    false
    );

    dma_in_ctrl = dma_claim_unused_channel(true);
    dma_in_data = dma_claim_unused_channel(true);
    dma_channel_config e = dma_channel_get_default_config(dma_in_ctrl);
    channel_config_set_transfer_data_size(&e, DMA_SIZE_32);
    channel_config_set_ring(&e, false, 3); // this means 64 bits before wrapping //still a write, this time writing to write addr
    channel_config_set_read_increment(&e, true);
    channel_config_set_write_increment(&e, false);

    dma_channel_configure(dma_in_ctrl,
    &e,
    &dma_hw->ch[dma_in_data].al2_write_addr_trig, //alias to write address trigger of data out dma
    inputcontrol, //pointy pointy
    1,
    false
);
    dma_channel_config f = dma_channel_get_default_config(dma_in_data);
    channel_config_set_transfer_data_size(&f, DMA_SIZE_32);
    channel_config_set_read_increment(&f, false);
    channel_config_set_write_increment(&f, true);
    channel_config_set_dreq(&f, pio_get_dreq(pio, smin, false));
    channel_config_set_chain_to(&f, dma_in_ctrl);

    dma_channel_configure(dma_in_data,
        &f,
        NULL,
        &pio->rxf[smin],
        AUDIO_BUFFER_SIZE,
        false
    );



//set up interrupts
dma_channel_set_irq0_enabled(dma_out_data, true);
irq_set_exclusive_handler(DMA_IRQ_0,dmahandler);
irq_set_enabled(DMA_IRQ_0, true);



//start dma ctrl channels
dma_channel_start(dma_out_ctrl);
dma_channel_start(dma_in_ctrl);

printf("dma init finished");

}

void passthrough(uint32_t * input, uint32_t * output){
    //take safe input, put in safe output
    for (int i = 0 ; i < AUDIO_BUFFER_SIZE; i++){
    input[i] = output[i];
    }
}

void dmahandler(){
    
  //printf("irq: %d\n", dma_hw->ch[dma_out_data].read_addr);
    if(*(uint32_t**)dma_hw->ch[dma_out_ctrl].read_addr == outputbuffer){
        passthrough(inputbuffer, outputbuffer);
        //printf("irq: if\n");
    }
    else{
        passthrough(&inputbuffer[AUDIO_BUFFER_SIZE], &outputbuffer[AUDIO_BUFFER_SIZE]);
       // printf("irq: else\n");

    }
    dma_hw->ints0 = 1u << dma_out_data;
}








void pio_i2s_init(PIO pio, uint sm, uint smsck, uint smin, uint offset,uint sckoffset,uint inoffset){


    // config sck sm
    pio_gpio_init(pio, SCK);
    
    pio_sm_config d = i2s_program_get_default_config(sckoffset);
    sm_config_set_wrap(&d,sckoffset+1,sckoffset+2); // not setting this = world of pain
    sm_config_set_set_pins(&d, SCK,1);
    
    sm_config_set_clkdiv(&d, 8.138020833333333); // 6.103515625  should be correct for 512 * fs which may be way too fast. or 8.138020833333333 for 384
    pio_sm_set_consecutive_pindirs(pio, smsck, SCK, 1, true); // true = output
    pio_sm_set_pins(pio, smsck, 0);

    pio_sm_init(pio, smsck, sckoffset, &d);


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

    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_pins_with_mask(
        pio,
        sm,
        0,
        (1u << DATA_OUT) | (1u << BCLK) | (1u << WS) 
    );

    pio_sm_exec(pio, sm, pio_encode_jmp(offset + i2s_offset_entry_point)); // points the program to start at our entry point.

// data in stuff on a hope and a prayer

pio_gpio_init(pio, DATA_IN);
pio_sm_config i = i2s_program_get_default_config(inoffset);
pio_sm_set_consecutive_pindirs(pio, smin, DATA_IN, 1, false);
sm_config_set_in_pins(&i, DATA_IN);
sm_config_set_in_shift(&i, true, true, 32); // assumption that right shift makes sense as its opposite to dac. Also autopush
sm_config_set_clkdiv(&i, 48.828125f);
sm_config_set_fifo_join(&i, PIO_FIFO_JOIN_RX);
pio_sm_init(pio, smin, inoffset, &i);
pio_sm_set_pins(pio, smin, 0);


pio_sm_exec(pio, sm, pio_encode_jmp(inoffset + i2sin_offset_entry_point)); // points the program to start at our entry point.


}

int main()
{
    stdio_init_all();
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint smsck = pio_claim_unused_sm(pio, true);
    uint smin = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &i2s_program);
    uint sckoffset = pio_add_program(pio, &piosck_program);
    uint inoffset = pio_add_program(pio, &i2sin_program);

    pio_i2s_init(pio, sm, smsck,smin, offset, sckoffset, inoffset);
    pio_sm_set_enabled(pio,sm, true);
    pio_sm_set_enabled(pio,smsck, true);
    pio_sm_set_enabled(pio,smin, true);


    dmasetup(pio, sm, smin);
    //dmahandler();

    while(true){
        //printf("fifo: %d\n", pio_sm_get_tx_fifo_level ( pio,  sm));
      
        
    }

    
}
