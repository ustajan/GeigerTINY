/*
Author:  Areg Danagoulian
email: aregjan@mit.edu
License: see LICENSE in the main repo directory
Description:
 + Produces a square pulse of a variable duty to operate the boost converter of the Geiger counter
 + Reads in the pulses from the SBM-20 Geiger tube
 + Performs preliminary analysis and displays those on an oled display
 + Reads in the HV value from the 1M/1G voltage divider.  Adjusts duty to achieve HV in the desired window
*/

#include <Tiny4kOLED_bitbang.h>
#define OLED
#define LED_PIN 4
#define HV_OUT_PIN 1
#define HV_INPUT_PIN 5
#define INTERRUPT_PIN 3
#define HVread
//#define Blinker // in the current implementation LED output is disabled, as the pin LED_PIN is currently 
// used for NV_INPUT_PIN (the numbering for attiny85's input are different for analog input and digital output).
// If you choose to enable Blinker, uncomment the line above, comment the HVread line above it.  You'll also need
// to change the circuitry appropriately.

volatile uint32_t stack[32];  // stack is not used -- it's only used for serial output, which is not enabled here
volatile uint8_t stack_top = 0;
volatile byte i = 0; //the state of the input interrupt
uint32_t count=0,count_total=0;
uint32_t count_refresh = 1;
uint32_t display_refresh_time=10; //in seconds
unsigned long last_time=0;
unsigned long last_led_on=0;
float max_dose=0; //maximum observed dose, in uR/hr
float total_dose=0; //total dose, in uR
bool first_time=true;
volatile bool led_blink_on=false,led_blink_arm=true;

void setup() {
  // put your setup code here, to run once:
#ifdef Blinker  
  pinMode(LED_PIN, OUTPUT);
#endif
  pinMode(HV_OUT_PIN, OUTPUT);
  pinMode(HV_INPUT_PIN,INPUT);
#ifdef HVread
  pinMode(A2,INPUT); //pb5, analog input
  analogReference( INTERNAL2V56_NO_CAP ); //2.56 V internal reference
#endif
 
  cli();
  MCUCR |= (1 << ISC00);  //CHANGE mode
  GIMSK = 0b00100000;  //enable all pins
  PCMSK = 0b00001000;  //enable external interrupt to pb3
// PCMSK |= (1 << PCINT3);    // Enable interrupt handler (ISR) for our chosen interrupt pin (PCINT1/PB1/pin 6)
//  GIMSK |= (1 << PCIE);             // Enable PCINT interrupt in the general interrupt mask
  pinMode(INTERRUPT_PIN, INPUT);
  sei();                 // enables interrupts


  //setup oled
#ifdef OLED
  oled.begin(128, 64, sizeof(tiny4koled_init_defaults), tiny4koled_init_defaults);
  oled.enableChargePump();  // The default is off, but most boards need this.
  oled.setFont(FONT8X16);  //font6x8, font8x16
  oled.setRotation(1);
  oled.clear();
  oled.on();
  oled.print(F("GeigerTINY\nwelcomes you!\nVisit us at\nlanph.mit.edu\n"));
#endif
}

void loop() {


  hv_out();
  blinker();
  if(millis()%(display_refresh_time*1000)<50){
#ifdef OLED
   updateDisplay();
#endif
    count=0; //reset the counter
  }
  

}

ISR(PCINT0_vect) {
  i = digitalRead(INTERRUPT_PIN);
  if(i) {
    led_blink_on=true;
  }

}
#ifdef OLED
void updateDisplay() {
  oled.clear();
  uint32_t cpm = count*60/display_refresh_time;
  oled.print(F("uR/hr: "));   oled.print(cpm*0.57);
  oled.print(F("\nCPM: ")); oled.print(cpm);
  oled.print("\nTot: "); oled.print(count_total);
  oled.setCursor(0, 6);
}
#endif
void detect() {
/*
  if(stack_top<32){
  	stack[stack_top] = micros();
  	++stack_top;
  } */
  ++count; //this gets reset every time the rates are displayed, in display_count()
  ++count_total;  
  
  
}
void blinker(){ //operate the LED on/off


  if(led_blink_on && led_blink_arm){
#ifdef Blinker
   digitalWrite(LED_PIN, HIGH);
#endif
   led_blink_arm=false;
   last_led_on=micros64();
   detect();
#ifdef OLED
    oled.setCursor(0, 6);
    oled.print("count!");
    oled.setCursor(0, 6);
    oled.print("      "); //erase
#endif   
  }
  if(led_blink_on && !led_blink_arm && micros64()-last_led_on>1e+5){ //stay on for 1 sec
#ifdef Blinker
    digitalWrite(LED_PIN, LOW);
#endif
    led_blink_arm=true;
    led_blink_on=false;     
  }
  
}

void hv_out(){
  unsigned long time;
  static uint32_t last_high=0,last_low=0;

//note to self:  0.1 and 1500 gives 320V
  static float duty = 0.070; // the duty of the pulse out. Subject to variations via feedback from the HV
  uint32_t T = 1500; //the period of the pulse in microseconds

  time=micros64();

  if(time-last_high>T){
//    digitalWrite(HV_OUT_PIN,HIGH);    
    PORTB |= 1<<HV_OUT_PIN; //turn on PB1, same as above but direct register operation
    delayMicroseconds(T*duty); //this is blocking, however this is very precise
//    digitalWrite(HV_OUT_PIN,LOW);
    PORTB ^= 1<<HV_OUT_PIN; //turn off PB1, same as above but direct register operation
    last_high=time;
  }

//now check the HV, and try to correct the duty
  const float hv_high_value=400; //max value of HV, in volts
  const float hv_low_value =370; //min value of HV, in volts


#ifdef HVread
  float v;
  if(millis()%100<2 && time>1e+6){
//    ADCSRA |= (1 << ADSC);         //start conversion
    uint16_t adc=analogRead(A2); //2^10 ADC
    v=(1000./1.2)*adc*2.56/pow(2,10); //convert to voltage in V
    if(v > hv_high_value && duty>0.0004) duty-=0.0004;
    else if(v < hv_low_value && duty < 0.9996) duty+=0.0004; //increment duty to keep voltage within range. 
  } 
  if(millis()%2000<2) {
    oled.setCursor(50, 6);
    oled.print("HV: ");
    oled.print(int(v));
//    oled.print(duty);
    oled.print("\r");
    }
#endif  

}
uint64_t micros64() { 
  // we need this to fix the problem of micros() rollover every 2^32.  
    static uint64_t last_time,rollover_correction=0;
    uint64_t time64;

    if(micros()<last_time){
      rollover_correction+=2^32;
    }
    last_time=micros();
    time64=micros()+rollover_correction;
    return time64;
}


