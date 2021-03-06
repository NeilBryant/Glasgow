#include <fx2regs.h>
#include <fx2i2c.h>
#include "glasgow.h"

void iobuf_init_dac_ldo() {
  // Configure I/O buffer pins as open-source/open-drain; they have 100k pulls
  IOD &= ~((1<<PIND_ENVA)|(1<<PIND_ENVB));
  IOD |=                                 (1<<PIND_OEQ_N);
  OED |=  ((1<<PIND_ENVA)|(1<<PIND_ENVB)|(1<<PIND_OEQ_N));

  // Enable I/O buffers
  IOD &= ~ (1<<PIND_OEQ_N);
}

static bool dac_start(uint8_t mask, bool read) {
  uint8_t addr = 0;
  switch(mask) {
    case IO_BUF_A:   addr = I2C_ADDR_IOA_DAC; break;
    case IO_BUF_B:   addr = I2C_ADDR_IOB_DAC; break;
    case IO_BUF_ALL: if(!read) addr = I2C_ADDR_ALL_DAC; break;
  }
  if(!addr)
    return false;

  if(!i2c_start((addr<<1)|read)) {
    i2c_stop();
    return false;
  }

  return true;
}

bool iobuf_set_voltage(uint8_t mask, __xdata const uint16_t *millivolts_ptr) {
  uint8_t pin_mask = 0;
  uint16_t millivolts = *millivolts_ptr;
  uint16_t code_word;
  __pdata uint8_t code_bytes[2];

  // Which LDO enable pins do we touch?
  if(mask & IO_BUF_A) pin_mask |= 1<<PIND_ENVA;
  if(mask & IO_BUF_B) pin_mask |= 1<<PIND_ENVB;

  // Just disable the LDOs, DAC power is irrelevant
  if(millivolts == 0) {
    IOD &= ~pin_mask;
    return true;
  }

  // Compute the DAC code word
  if(millivolts < MIN_VOLTAGE || millivolts > MAX_VOLTAGE)
    return false;

  // Offset 1650, slope -15.2, 0x1000/15.2 = 269
  // The DAC has a 12-bit code word, so we only shift back by 8
  code_word = (254 << 4) - ((((millivolts - 1650) >> 4) * 269) >> 4);
  code_bytes[0] = code_word >> 8;
  code_bytes[1] = code_word & 0xff;

  // Send the DAC code word
  if(!dac_start(mask, /*read=*/false))
    return false;
  if(!i2c_write(code_bytes, sizeof(code_bytes))) {
    i2c_stop();
    return false;
  }
  if(!i2c_stop())
    return false;

  // Enable LDO(s)
  IOD |= pin_mask;

  return true;
}

bool iobuf_get_voltage(uint8_t selector, __xdata uint16_t *millivolts_ptr) {
  uint8_t pin_mask = 0;
  uint16_t code_word;
  __pdata uint8_t code_bytes[2];

  // Which LDO enable pins do we look at?
  switch(selector) {
    case IO_BUF_A: pin_mask = 1<<PIND_ENVA; break;
    case IO_BUF_B: pin_mask = 1<<PIND_ENVB; break;
    default: return false;
  }

  // Check if LDO is disabled
  if(!(IOD & pin_mask)) {
    *millivolts_ptr = 0;
    return true;
  }

  if(!dac_start(selector, /*read=*/true))
    return false;
  if(!i2c_read(code_bytes, sizeof(code_bytes)))
    return false;

  // See explanation in iobuf_set_voltage.
  code_word = (((uint16_t)code_bytes[0]) << 8) | code_bytes[1];
  *millivolts_ptr = 1650 + (255 - (code_word >> 4)) * 152 / 10;

  return true;
}
