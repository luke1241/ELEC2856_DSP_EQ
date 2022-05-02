#include "mbed.h"                                                   //Library Inclusions
#include "rtos.h"
#include "LowPass.h"
#include "HighPass.h"
#include "BandPass.h"
#include <chrono>
#include <cstdio>

#define PIN_INPUT           PC_0                                    //Pin definitions
#define PIN_OUTPUT         PA_4
#define PIN_INDICATOR       PB_9
#define PIN_LOW_GAIN        PA_1
#define PIN_MID_GAIN        PA_7
#define PIN_HIGH_GAIN       PB_0
#define PIN_LOW_FREQ        PA_0
#define PIN_HIGH_FREQ       PC_1
#define PIN_LOW_MODE        PB_8
#define PIN_HIGH_MODE       PC_9

#define LOW_FREQ_MIN        20.0f                                   //Constant definitions
#define LOW_FREQ_MAX        300.0f
#define HIGH_FREQ_MIN       2000.0f
#define HIGH_FREQ_MAX       10000.0f
#define OFFSET              1/3.3

BufferedSerial pc(USBTX, USBRX);

InterruptIn     lowModeIn(PIN_LOW_MODE, PullDown);                  //Low mode interupt object 
InterruptIn     highModeIn(PIN_HIGH_MODE, PullDown);                //High mode interrupt object
AnalogIn        inputSig(PIN_INPUT);                                //Input/output signal objects
AnalogOut       outputSig(PIN_OUTPUT);
DigitalOut      indicator(PIN_INDICATOR);                           //Indicator LED shows program is running
AnalogIn        lowAmplitude_in(PIN_LOW_GAIN);                      //Control input objects
AnalogIn        lowFrequency_in(PIN_LOW_FREQ);
AnalogIn        highAmplitude_in(PIN_HIGH_GAIN);
AnalogIn        highFrequency_in(PIN_HIGH_FREQ);
AnalogIn        midAmplitude_in(PIN_MID_GAIN);
int             g_lowModeFlag =     0;                              //0 = low shelf, 1 = low cut
int             g_highModeFlag =    0; 
float           input =             0.0f;                           //Input/output signal value variables
float           output =            0.0f;
float           sampleFreq =        48000.0f;                       //Ideal sample frequency
int             sampleInterval_us = int(1000000/sampleFreq);        //Sample period in microseconds
int             sampleFreq_actual = 1000000/sampleInterval_us;      //Actual sample frequency given rounded sample period
int             controlFreq =       10000;                          //Number of iterations between control updates
Timer           t;                                                  //Timer variable to keep track of time

LowPass         lpFilt(50.0f, sampleFreq_actual, 1.0f);             //Filter declarations
HighPass        hpFilt(150.0f, sampleFreq_actual, 1.0f);
BandPass        bpFilt(50.0f, 150.0f, sampleFreq_actual, 1.0f);

void            process_eq();                                       //Function declarations  
void            update_controls();
float           scale(float in, float in_min, float in_max, float out_min, float out_max);
void            lowMode_isr_rise();
void            lowMode_isr_fall();
void            highMode_isr_rise();
void            highMode_isr_fall();

int main()                                                          //Main function
{  
    lowModeIn.rise(&lowMode_isr_rise);                              //Linking interrupts to corresct ISRs
    lowModeIn.fall(&lowMode_isr_fall);
    highModeIn.rise(&highMode_isr_rise);
    highModeIn.fall(&highMode_isr_fall);
    g_highModeFlag = highModeIn.read();                             //Set flag values to match initial state of switches
    g_lowModeFlag = lowModeIn.read();

    int ticks = 0;                                                  //Initialize ticks to 0
    t.start();                                                      //Begin timer
    auto us = t.elapsed_time().count();                             //Initialize us to initial time: https://forums.mbed.com/t/how-to-use-elapsed-time-count/10600/4
    auto us_old = us;                                               //Initialize us_old to initial time
    
    while(true){                                                    
        us = t.elapsed_time().count();                              //Update us to current time passed
        if(us - us_old >= sampleInterval_us) {                      //If time passed >= sampling period
            us_old = us_old + sampleInterval_us;                    //us_old increases by sample period

            process_eq();                                           //Update outputs based on inputs

            ticks++;                                                //Increment ticks
            if(ticks % int(sampleFreq_actual) == 0) {               //Every second
                indicator = !indicator;                             //Toggle indicator LED
                ticks = 0;                                          //reset ticks
            }
            if(ticks % controlFreq == 0) {                          //If time = control update time
                update_controls();                                  //Update controls
            }
        }
    }
}

void process_eq() {                                                 //Function to handle EQ filtering
    input = inputSig.read() - OFFSET;                                                                                   //Read input and remove DC offset 
    output = (hpFilt.update(input) * g_highModeFlag) + (lpFilt.update(input) * g_lowModeFlag) + bpFilt.update(input);   //Update each filter with input signal, account for filter modes
    outputSig.write(output + OFFSET);                                                                                   //Output filtered right signal
    
                                                                             
}

void update_controls() {                                            //Function to handle reading control values and updating filters
    int lowCut = scale(lowFrequency_in.read(), 0.0f, 1.0f, LOW_FREQ_MIN, LOW_FREQ_MAX);                                 //Calculate cutoff frequencies based on control positions
    int highCut = scale(highFrequency_in.read(), 0.0f, 1.0f, HIGH_FREQ_MIN, HIGH_FREQ_MAX);

    lpFilt.set_gain(lowAmplitude_in.read());                                                                            //Set gain of   low pass
    hpFilt.set_gain(highAmplitude_in.read());                                                                           //              high pass
    bpFilt.set_gain(midAmplitude_in.read());                                                                            //              band pass
    lpFilt.set_cutoff(lowCut);                                                                                          //Set cutoff of low pass
    hpFilt.set_cutoff(highCut);                                                                                         //              high pass
    bpFilt.set_cutoff(lowCut, highCut);                                                                                 //              band pass
//  printf("Low G: %i,    Mid G: %i,    High G: %i\n", (int)(lowAmplitude_in.read() * 100), (int)(midAmplitude_in.read() * 100), (int)(highAmplitude_in.read() * 100));
}

float scale(float in, float in_min, float in_max, float out_min, float out_max) {                                       //Function to handle scaling input value based on given input/output range
    float in_percent = (in - in_min) / (in_max - in_min);                                                               //Input percentage of input range
    return (in_percent * (out_max - out_min)) + out_min;                                                                //Output = input percentage of output range offset by minimum
}

void lowMode_isr_rise() {                                           //ISR function definitions
    g_lowModeFlag = 1;
}
void lowMode_isr_fall() {
    g_lowModeFlag = 0;
}

void highMode_isr_rise() {
    g_highModeFlag = 1;
}
void highMode_isr_fall() {
    g_highModeFlag = 0;
}
