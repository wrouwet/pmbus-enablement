/*
 * PMBus number formats and the SMBus packet error code (PEC).
 *
 * Values cross this API as signed integers in milli-units (mV, mA, mW, m°C), so the
 * library needs no floating point.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PMBUS_FORMAT_H_
#define PMBUS_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

/* CRC-8, polynomial x^8 + x^2 + x + 1, initial value 0, as SMBus 3.x section 6.4 defines. */
uint8_t pmbus_crc8_byte(uint8_t crc, uint8_t byte);
uint8_t pmbus_crc8(uint8_t crc, const uint8_t *buf, size_t len);

/*
 * LINEAR11: a 5-bit two's-complement exponent N in bits 15:11 and an 11-bit
 * two's-complement mantissa Y in bits 10:0; the value is Y * 2^N. The encoder picks the
 * smallest N that fits, which keeps the most precision. Decoding saturates: the largest
 * LINEAR11 and ULINEAR16 values exceed the int32_t milli-unit range.
 */
uint16_t pmbus_linear11_encode(int32_t milli);
int32_t pmbus_linear11_decode(uint16_t word);

/*
 * ULINEAR16, used for output voltages: a 16-bit unsigned mantissa whose exponent comes
 * from VOUT_MODE bits 4:0. The encoder clamps to 0..0xFFFF.
 */
uint16_t pmbus_ulinear16_encode(int32_t milli, int8_t exponent);
int32_t pmbus_ulinear16_decode(uint16_t word, int8_t exponent);

/* The exponent field of a linear-mode VOUT_MODE byte. */
int8_t pmbus_vout_mode_exponent(uint8_t vout_mode);

#endif /* PMBUS_FORMAT_H_ */
