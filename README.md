# WireZeroCopy
Zero copy I2C Wire-like library
based on standard Arduino Wire library
Implements user-defined shared buffer for both TwoWire and twi_ library levels up to 255 bytes.
Has zero-copy
mode when data byffer is shared between TwoWire and twi_ libraries. 
Wire.begin();

Just use WireZ instead of Wire
and WireZ.h instead of Wire.h

Benefits: 
* up to 255 bytes max transfer length with user-specified buffer 
* memory usage reduced to user buffer size + 2x32 default built-in buffers (can be reduced to 2x4)
* no additional memory copy operations

Zero Copy mode is managed via 
void setUserBuffer(uint8_t*, uint8_t, bool twi_zero_copy = false);
e.g.

Wire.begin();
WireZ.setUserBuffer(&user_bufer, sizeof(user_bufer), true);

