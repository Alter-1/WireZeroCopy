/*
  WireZ.cpp - ZeroCopy TWI/I2C library for Arduino & Wiring by Alexandr Telyiatnikov (alter@alter.org.ua)

  derived from
  TwoWire.cpp - TWI/I2C library for Wiring & Arduino
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 
  Modified 2012 by Todd Krein (todd@krein.org) to implement repeated starts
  Modified 2017 by Chuck Todd (ctodd@cableone.net) to correct Unconfigured Slave Mode reboot
  Modified 2020 by Greyson Christoforo (grey@christoforo.net) to implement timeouts
  Modified 2023 by Alexandr Telyiatnikov (alter@alter.org.ua) to implement user-defined external buffer and use single buffer for rx/tx
*/

extern "C" {
  #include <stdlib.h>
  #include <string.h>
  #include <inttypes.h>
  #include "twiz.h"
}

#include "WireZ.h"

// Initialize Class Variables //////////////////////////////////////////////////
uint8_t TwoWireZ::xBuffer[BUFFER_LENGTH];
bool TwoWireZ::bUserBuffer = false;
uint8_t TwoWireZ::X_BUFFER_LENGTH = BUFFER_LENGTH;

uint8_t* TwoWireZ::rxBuffer = &xBuffer[0];
uint8_t TwoWireZ::rxBufferIndex = 0;
uint8_t TwoWireZ::rxBufferLength = 0;

uint8_t TwoWireZ::txAddress = 0;
uint8_t* TwoWireZ::txBuffer = &xBuffer[0];
uint8_t TwoWireZ::txBufferIndex = 0;
uint8_t TwoWireZ::txBufferLength = 0;

uint8_t TwoWireZ::transmitting = 0;
void (*TwoWireZ::user_onRequest)(void);
void (*TwoWireZ::user_onReceive)(int);

// Constructors ////////////////////////////////////////////////////////////////

TwoWireZ::TwoWire()
{
}

// Public Methods //////////////////////////////////////////////////////////////

void TwoWireZ::begin(void)
{
  rxBufferIndex = 0;
  rxBufferLength = 0;

  txBufferIndex = 0;
  txBufferLength = 0;

  twi_init();
  twi_attachSlaveTxEvent(onRequestService); // default callback must exist
  twi_attachSlaveRxEvent(onReceiveService); // default callback must exist
}

void TwoWireZ::begin(uint8_t address)
{
  begin();
  twi_setAddress(address);
}

void TwoWireZ::begin(int address)
{
  begin((uint8_t)address);
}

void TwoWireZ::end(void)
{
  twi_disable();
}

void TwoWireZ::setUserBuffer(uint8_t* uBuffer, uint8_t uLength, bool twi_zero_copy)
{
  if(uBuffer && uLength) {
    bUserBuffer = true;
    X_BUFFER_LENGTH = uLength;
    rxBuffer = uBuffer;
    txBuffer = uBuffer;
  } else {
    twi_zero_copy = false;
    bUserBuffer = false;
    X_BUFFER_LENGTH = BUFFER_LENGTH;
    rxBuffer = &xBuffer[0];
    txBuffer = &xBuffer[0];
  }
  if(twi_zero_copy) {
      twi_setUserBuffer(uBuffer, uLength);
  } else {
      twi_setUserBuffer(NULL, 0);
  }
}

void TwoWireZ::setClock(uint32_t clock)
{
  twi_setFrequency(clock);
}

/***
 * Sets the TWI timeout.
 *
 * This limits the maximum time to wait for the TWI hardware. If more time passes, the bus is assumed
 * to have locked up (e.g. due to noise-induced glitches or faulty slaves) and the transaction is aborted.
 * Optionally, the TWI hardware is also reset, which can be required to allow subsequent transactions to
 * succeed in some cases (in particular when noise has made the TWI hardware think there is a second
 * master that has claimed the bus).
 *
 * When a timeout is triggered, a flag is set that can be queried with `getWireTimeoutFlag()` and is cleared
 * when `clearWireTimeoutFlag()` or `setWireTimeoutUs()` is called.
 *
 * Note that this timeout can also trigger while waiting for clock stretching or waiting for a second master
 * to complete its transaction. So make sure to adapt the timeout to accomodate for those cases if needed.
 * A typical timeout would be 25ms (which is the maximum clock stretching allowed by the SMBus protocol),
 * but (much) shorter values will usually also work.
 *
 * In the future, a timeout will be enabled by default, so if you require the timeout to be disabled, it is
 * recommended you disable it by default using `setWireTimeoutUs(0)`, even though that is currently
 * the default.
 *
 * @param timeout a timeout value in microseconds, if zero then timeout checking is disabled
 * @param reset_with_timeout if true then TWI interface will be automatically reset on timeout
 *                           if false then TWI interface will not be reset on timeout

 */
void TwoWireZ::setWireTimeout(uint32_t timeout, bool reset_with_timeout){
  twi_setTimeoutInMicros(timeout, reset_with_timeout);
}

/***
 * Returns the TWI timeout flag.
 *
 * @return true if timeout has occured since the flag was last cleared.
 */
bool TwoWireZ::getWireTimeoutFlag(void){
  return(twi_manageTimeoutFlag(false));
}

/***
 * Clears the TWI timeout flag.
 */
void TwoWireZ::clearWireTimeoutFlag(void){
  twi_manageTimeoutFlag(true);
}

uint8_t TwoWireZ::requestFrom(uint8_t address, uint8_t quantity, uint32_t iaddress, uint8_t isize, uint8_t sendStop)
{
  if (isize > 0) {
    // send internal address; this mode allows sending a repeated start to access
    // some devices' internal registers. This function is executed by the hardware
    // TWI module on other processors (for example Due's TWI_IADR and TWI_MMR registers)

    beginTransmission(address);

    // the maximum size of internal address is 3 bytes
    if (isize > 3){
      isize = 3;
    }

    // write internal register address - most significant byte first
    /*
    while (isize-- > 0)
      write((uint8_t)(iaddress >> (isize*8)));
    }
    */
    // we are switched to zero-copy model, so, avoid single byte writes, use pre-filled buffers
    uint8_t ibuff[4];
    ibuff[2] = iaddress & 0xff;
    ibuff[1] = (iaddress >> 8) & 0xff;
    ibuff[0] = (iaddress >> 16) & 0xff;

    if(bUserBuffer) {
      twi_setUserBuffer(NULL, 0); // switch to buffered mode to prevent shared buffer corruption
    }

    write(&ibuff[0], isize);
  
    endTransmission(false);

    if(bUserBuffer) {
      twi_setUserBuffer(txBuffer, X_BUFFER_LENGTH); // revert ZeroCopy mode
    }

  }
  // clamp to buffer length
  if(quantity > X_BUFFER_LENGTH){
    quantity = X_BUFFER_LENGTH;
  }
  // perform blocking read into buffer. If userBuffer is used, rxBuffer already points there
  uint8_t read = twi_readFrom(address, rxBuffer, quantity, sendStop);
  // set rx buffer iterator vars
  rxBufferIndex = 0;
  rxBufferLength = read;

  return read;
}

uint8_t TwoWireZ::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop) {
	return requestFrom((uint8_t)address, (uint8_t)quantity, (uint32_t)0, (uint8_t)0, (uint8_t)sendStop);
}

uint8_t TwoWireZ::requestFrom(uint8_t address, uint8_t quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWireZ::requestFrom(int address, int quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWireZ::requestFrom(int address, int quantity, int sendStop)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)sendStop);
}

void TwoWireZ::beginTransmission(uint8_t address)
{
  // indicate that we are transmitting
  transmitting = 1;
  // set address of targeted slave
  txAddress = address;
  // reset tx buffer iterator vars
  txBufferIndex = 0;
  txBufferLength = 0;
}

void TwoWireZ::beginTransmission(int address)
{
  beginTransmission((uint8_t)address);
}

//
//	Originally, 'endTransmission' was an f(void) function.
//	It has been modified to take one parameter indicating
//	whether or not a STOP should be performed on the bus.
//	Calling endTransmission(false) allows a sketch to 
//	perform a repeated start. 
//
//	WARNING: Nothing in the library keeps track of whether
//	the bus tenure has been properly ended with a STOP. It
//	is very possible to leave the bus in a hung state if
//	no call to endTransmission(true) is made. Some I2C
//	devices will behave oddly if they do not see a STOP.
//
uint8_t TwoWireZ::endTransmission(uint8_t sendStop)
{
  // transmit buffer (blocking)
  uint8_t ret = twi_writeTo(txAddress, txBuffer, txBufferLength, 1, sendStop);
  // reset tx buffer iterator vars
  txBufferIndex = 0;
  txBufferLength = 0;
  // indicate that we are done transmitting
  transmitting = 0;
  return ret;
}

//	This provides backwards compatibility with the original
//	definition, and expected behaviour, of endTransmission
//
uint8_t TwoWireZ::endTransmission(void)
{
  return endTransmission(true);
}

// must be called in:
// slave tx event callback
// or after beginTransmission(address)
// all data is buffered and sent to slave on endTransmission(), we are switching to zero-copy model, so avoid using it
size_t TwoWireZ::write(uint8_t data)
{
  if(transmitting){
  // in master transmitter mode

    // don't bother if buffer is full
    // we allow only direct user-buffer writes
    if(txBufferLength >= X_BUFFER_LENGTH){
      setWriteError();
      return 0;
    }

    // put byte in tx buffer
    // Note !!! use writeZeroCopy() if you send date from prepared buffer
    // Otherwise we expect txBuffer and actual source buffer are not overlapping
    txBuffer[txBufferIndex] = data;
    ++txBufferIndex;
    // update amount in buffer   
    txBufferLength = txBufferIndex;
  }else{
  // in slave send mode
    // reply to master
    twi_transmit(&data, 1);
  }
  return 1;
}

// must be called in:
// slave tx event callback
// or after beginTransmission(address)
size_t TwoWireZ::write(const uint8_t *data, size_t quantity)
{
  if(transmitting){
  // in master transmitter mode
    for(size_t i = 0; i < quantity; ++i){
      write(data[i]);
    }
  }else{
  // in slave send mode
    // reply to master
    twi_transmit(data, quantity);
  }
  return quantity;
}

// sends data from prepared user buffer
// must be called in:
// slave tx event callback
// or after beginTransmission(address)
// user buffer must be set up with setUserBuffer()
size_t TwoWireZ::writeZeroCopy(size_t quantity)
{
  if(transmitting){
  // in master transmitter mode
    if(txBufferLength >= X_BUFFER_LENGTH ||
       !bUserBuffer){
      setWriteError();
      return 0;
    }

    txBufferIndex=quantity;
    // update amount in buffer   
    txBufferLength = txBufferIndex;
  }else{
  // in slave send mode
    // reply to master frem prepared buffer
    twi_transmit(txBuffer, quantity);
  }
  return quantity;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWireZ::available(void)
{
  return rxBufferLength - rxBufferIndex;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWireZ::read(void)
{
  int value = -1;
  
  // get each successive byte on each call
  if(rxBufferIndex < rxBufferLength){
    value = rxBuffer[rxBufferIndex];
    ++rxBufferIndex;
  }

  return value;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWireZ::peek(void)
{
  int value = -1;
  
  if(rxBufferIndex < rxBufferLength){
    value = rxBuffer[rxBufferIndex];
  }

  return value;
}

void TwoWireZ::flush(void)
{
  // XXX: to be implemented.
}

// behind the scenes function that is called when data is received
void TwoWireZ::onReceiveService(uint8_t* inBytes, int numBytes)
{
  // don't bother if user hasn't registered a callback
  if(!user_onReceive){
    return;
  }
  // don't bother if rx buffer is in use by a master requestFrom() op
  // i know this drops data, but it allows for slight stupidity
  // meaning, they may not have read all the master requestFrom() data yet
  if(rxBufferIndex < rxBufferLength){
    return;
  }
  // copy twi rx buffer into local read buffer
  // this enables new reads to happen in parallel
  for(uint8_t i = 0; i < numBytes; ++i){
    rxBuffer[i] = inBytes[i];    
  }
  // set rx iterator vars
  rxBufferIndex = 0;
  rxBufferLength = numBytes;
  // alert user program
  user_onReceive(numBytes);
}

// behind the scenes function that is called when data is requested
void TwoWireZ::onRequestService(void)
{
  // don't bother if user hasn't registered a callback
  if(!user_onRequest){
    return;
  }
  // reset tx buffer iterator vars
  // !!! this will kill any pending pre-master sendTo() activity
  txBufferIndex = 0;
  txBufferLength = 0;
  // alert user program
  user_onRequest();
}

// sets function called on slave write
void TwoWireZ::onReceive( void (*function)(int) )
{
  user_onReceive = function;
}

// sets function called on slave read
void TwoWireZ::onRequest( void (*function)(void) )
{
  user_onRequest = function;
}

// Preinstantiate Objects //////////////////////////////////////////////////////

TwoWireZ WireZ = TwoWireZ();

