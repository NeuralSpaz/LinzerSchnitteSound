/*
    LinzerSchnitte Midi 2 RDS - Midi Interface for generating RDS for Linzer Schnitter
    Copyright (C) 2014  Ray Gardiner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>

    Complie with
	./make-i2c-bitbang
*/

#include <stdio.h>
#include <unistd.h>
#include "rpiGpio.h"


// clock speed in Hz
#define I2CSPEED 1000
#define SCL_PIN 3
#define SDA_PIN 2 


void I2C_delay() 
{ 
		usleep ( 1000000/I2CSPEED );
}

eState read_SCL (void) 
{
	eState state;
    gpioSetFunction(SCL_PIN, input);
    gpioReadPin(SCL_PIN, &state);
	return state;
}

eState read_SDA (void) 
{
	eState state;
    gpioSetFunction(SDA_PIN, input);
    gpioReadPin(SDA_PIN, &state);
	return state;
}


void clear_SCL (void) 
{
    gpioSetFunction(SCL_PIN, output);
	gpioSetPin(SCL_PIN, low);
}


void clear_SDA (void) 
{
    gpioSetFunction(SDA_PIN, output);
	gpioSetPin(SDA_PIN, low);
}

void arbitration_lost (void)
{
	printf("Arbitration Lost");
}
;
 
int started; 

void i2c_start_cond(void) {
  if (started) { // if started, do a restart cond
    // set SDA to 1
    read_SDA();
    I2C_delay();
    while (read_SCL() == 0) {  // Clock stretching
      // You should add timeout to this loop
    }
    // Repeated start setup time, minimum 4.7us
    I2C_delay();
  }
  if (read_SDA() == 0) {
    arbitration_lost();
  }
  // SCL is high, set SDA from 1 to 0.
  clear_SDA();
  I2C_delay();
  clear_SCL();
  started = 1;
}
 
void i2c_stop_cond(void){
  // set SDA to 0
  clear_SDA();
  I2C_delay();
  // Clock stretching
  while (read_SCL() == 0) {
    // add timeout to this loop.
  }
  // Stop bit setup time, minimum 4us
  I2C_delay();
  // SCL is high, set SDA from 0 to 1
  if (read_SDA() == 0) {
    arbitration_lost();
  }
  I2C_delay();
  started = 0;
}
 
// Write a bit to I2C bus
void i2c_write_bit(int bit) {
  if (bit) {
    read_SDA();
  } else {
    clear_SDA();
  }
  I2C_delay();
  while (read_SCL() == 0) { // Clock stretching
    // You should add timeout to this loop
  }
  // SCL is high, now data is valid
  // If SDA is high, check that nobody else is driving SDA
  if (bit && read_SDA() == 0) {
    arbitration_lost();
  }
  I2C_delay();
  clear_SCL();
}
 
// Read a bit from I2C bus
int i2c_read_bit(void) {
  int bit;
  // Let the slave drive data
  read_SDA();
  I2C_delay();
  while (read_SCL() == 0) { // Clock stretching
    // You should add timeout to this loop
  }
  // SCL is high, now data is valid
  bit = read_SDA();
  I2C_delay();
  clear_SCL();
  return bit;
}
 
// Write a byte to I2C bus. Return 0 if ack by the slave.
int i2c_write_byte(int send_start,
                    int send_stop,
                    unsigned char byte) {
  unsigned bit;
  int nack;
  if (send_start) {
    i2c_start_cond();
  }
  for (bit = 0; bit < 8; bit++) {
    i2c_write_bit((byte & 0x80) != 0);
    byte <<= 1;
  }
  nack = i2c_read_bit();
  if (send_stop) {
    i2c_stop_cond();
  }
  return nack;
}
 
// Read a byte from I2C bus
unsigned char i2c_read_byte(int nack, int send_stop) {
  unsigned char byte = 0;
  unsigned bit;
  for (bit = 0; bit < 8; bit++) {
    byte = (byte << 1) | i2c_read_bit();
  }
  i2c_write_bit(nack);
  if (send_stop) {
    i2c_stop_cond();
  }
  return byte;
}

void help ( void ) 
{
	printf("\n-R [addr]            read and print data from i2c register addr");
	printf("\n-W [addr] -d [data]  write data to i2c register addr");
}

#define RDS_CHIP_WRITE  0b11010110
#define RDS_CHIP_READ   0b11010111 

#define version "0.001 Beta" 

void LS_CMD ( int command, int address, int data ) 
{
	unsigned char B0,B1,C0,C1,D0,D1;

	B0 = 0x60;
	B1 = command & 0x1f;
	C0 = (address >> 8)&0xff;
	C1 = (address) & 0xff;
	D0 = (data>>8)&0xff;
	D1 = (data)&0xff;

	i2c_write_byte( 1,0,RDS_CHIP_WRITE);
	i2c_write_byte( 0,0,0x67);
	i2c_write_byte( 0,0,B0);
	i2c_write_byte( 0,0,B1);
	i2c_write_byte( 0,0,C0);
	i2c_write_byte( 0,0,C1);
	i2c_write_byte( 0,0,D0);
	i2c_write_byte( 0,0,D1);
	i2c_write_byte( 0,1,0x01);
}
	


