#!/usr/bin/env python3

# To set your DS3231 RTC module to generate a 1Hz square 
# wave output, you'll need to configure the control and 
# status registers inside the DS3231.
#
# The DS3231 has two important registers for this purpose:
# > Control Register (0x0E)
# > Status Register (0x0F)
# You need to set the Rate Select (RS) bits in the Control 
# Register to determine the frequency of the square wave. 
# Also, you might need to clear the Alarm Interrupt Enable
# bits to ensure the frequency output is not interrupted.

# Write to the Control Register:
# To generate a 1Hz square wave, you need to set the RS bits
# to 00 and ensure the INTCN bit is set to 1. This will select
# the 1Hz square wave frequency and enable the square wave output.

# Clear the Alarm Flags (if necessary):
# If the alarm interrupt flags are set, they might override 
# the square wave signal. You might need to clear these flags
# in the Status Register.

import smbus2

# Define the I2C bus
bus = smbus2.SMBus(1)

# DS3231 address on the I2C bus
DS3231_ADDR = 0x68

# Register addresses
CONTROL_REG = 0x0E
STATUS_REG = 0x0F

def set_1hz_output():
    # Read the current value of the control register
    control_reg_val = bus.read_byte_data(DS3231_ADDR, CONTROL_REG)

    # Set INTCN bit to 1 and RS bits to 00 (1Hz frequency)
    # INTCN is bit 2, RS2 is bit 4, RS1 is bit 3
    # Mask: 0b00000100 = 0x04
    control_reg_val = (control_reg_val & 0xE7) | 0x04

    # Write the new value to the control register
    bus.write_byte_data(DS3231_ADDR, CONTROL_REG, control_reg_val)

    # Clear the alarm flags in the status register by writing 0
    # It's usually safe to write 0 to the status register
    # as other bits are either read-only or self-clearing
    bus.write_byte_data(DS3231_ADDR, STATUS_REG, 0x00)

# Set the DS3231 to output 1Hz square wave
set_1hz_output()
