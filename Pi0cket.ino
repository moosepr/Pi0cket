/////////////////////////////////////////////////////////////////////
//situations we need to coope with
//
//system off, throw the switch - neeed to wait for boot, but if we get too far, we just kill
//
//system on, throw the switch - need to send the shutdown and wait for pi to go down
//
//system on, battery too low - need to send the shutdown and wiat for pi to go down
//
//system on, long press on power - force a shutdown
//
/////////////////////////////////////////////////////////////////////

#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <Adafruit_NeoPixel.h>
//some stolen constants defines for usage - http://www.technoblogy.com/show?KX0
#define adc_disable() (ADCSRA &= ~(1<<ADEN)) // disable ADC (before power-off)
#define adc_enable()  (ADCSRA |=  (1<<ADEN)) // re-enable ADC
//lets set some pins
#define pressPin 0
#define MOSFETpin 1
#define PixelPin 2
#define piPin 3
#define piControl 4
//and some constants
#define booting 1
#define isUp 2
#define goingDown 3

int pressCount,noPi,voltage,piState;
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PixelPin, NEO_GRB + NEO_KHZ800);

void setup() {
  //init the neopixel
  pixels.begin();
  pixels.setBrightness(50);
  //init the button
  pinMode( pressPin, INPUT_PULLUP);
  //PolyuHex.addPinTrigger( pressPin, LOW );
  /*lets sort the pi side*/
  //make the pi control pin about 3.3v so the pi sees it as up
  //the highest voltage we have here is 4.2 from a full battery
  //so each step will be 0.16v if we multiply that by 200 we are 
  //left with 3.2v. on a flat battery, we are only getting 3.2v
  //to start, so that takes each step to 0.125v, making our 200
  //become 2.5v and should still be seen as a HIGH pin  
  analogWrite(piControl,200);
  //have a pin we can watch for pi presence (maybe it can all be one pin?)
  pinMode( piPin, INPUT );
  /*now for the MOSFET*/
  pinMode(MOSFETpin,OUTPUT);
  digitalWrite(MOSFETpin,LOW);//make sure we are off to begin with
  //if we go straight to sleep on setup, then initial charging of a
  //protected battery will work, as the load will be low with the 
  //sleeping pi and the sleeping attiny
  doSleep();
}


void loop()
{
  delay(500);//slow things down
  //get the voltage as a nice number we can use
  //using the range of 3.3 to 4.2
  voltage = map(readVcc(),3300,4200,0,255);
  //lets set the pixel
  //the colour is set by red/green/blue, so if we
  //make red increase as the voltage drops, and green
  //match the voltage, we should get a green/orange/red
  //power LED!
  pixels.setPixelColor(0,255-voltage,voltage,0);
  //lets overwrite the color so we can tell the state
  if (noPi > 0 && noPi%2 == 0){
    if (piState == goingDown){
      //if we are going down, lets make it flash white
      pixels.setPixelColor(0,255,255,255); 
    }
    else{
      //we should be coming up, lets go blue
      pixels.setPixelColor(0,0,0,255); 
    }
  }
  pixels.show();
  //lets check for a keypress
  if (digitalRead(2) == HIGH) {
    pressCount = 0;
  }
  else {
    pressCount++;
  }
  //and now check there is a pi there
  if (analogRead(piPin) > 500){//we have a pi!!
    noPi = 0;
    digitalWrite(13, HIGH);//so we can see we have the pi
    piState = isUp;
  }
  else{
    noPi++;
    digitalWrite(13, LOW);//so we can see there is no pi
  }
  //we now have the data we need, lets see what we are trying to do
  //first lets check for a 'long press' to kill things
  if (pressCount > 50) {
    //the button has been down a while, lets just sleep
    delay(2000);//give us chance to let go
    doSleep();
  }
  //lets see if we are just wanting to turn things off
  if (pressCount > 10 && piState == isUp) {
    //we want to go down
    analogWrite(piControl,0);//make the output 0v so it triggers the shutdown
    piState = goingDown;
    //now we just need to wait for noPi
  }
  if (noPi > 10 && piState == goingDown) {
    //the pi should have been inactive for a few seconds
    doSleep();//we can go to sleep now
  }
  if (noPi > 100 && piState == booting) {
    //the pi is taking too long to boot, lets kill it
    doSleep();//we can go to sleep now
  }
}

void doSleep() {
  digitalWrite(MOSFETpin,LOW);//cut the power to le pi
  adc_disable(); // ADC uses ~320uA
  sleep_enable();
  attachInterrupt(pressPin, pin_isr, LOW);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  //maybe reset/clear the interupt here?
  sleep_disable();
  detachInterrupt(0);
  pixels.begin();
  pixels.setBrightness(50);  
  voltage = map(readVcc(),3300,4200,0,255);
  for (int i = 0 ; i < 10 ; i++) {
    //lets have a fast flash here so we know we are waking
    pixels.setPixelColor(0,255-voltage,voltage,0);
    pixels.show();
    delay(100);
    pixels.setPixelColor(0,0,0,0);
    pixels.show();
    delay(100);
  }
  //lets see if we still have a button pressed
  if(digitalRead(pressPin) == HIGH){
    //reset everything so we are ready
    pressCount = 0;
    noPi = 0;
    //init the neopixel
    pixels.begin();  
    analogWrite(piControl,200);//so the pi doesnt shut straight down
    piState = booting;
    digitalWrite(MOSFETpin,HIGH);//fire up le pi
  }
  else{
    //if the button is not still down, we need to go back to sleep
    doSleep();
  }
}

void pin_isr()
{
  sleep_disable();
  detachInterrupt(0);
}


long readVcc() {
  long result;
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif 

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1125300L / result; // Back-calculate AVcc in mV
  return result;
}
