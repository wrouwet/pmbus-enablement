/*
 * PMBus command codes and status bits, from PMBus Power System Management Protocol
 * Specification Part II, revision 1.3.1. Only the commands this project implements or
 * refers to are listed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PMBUS_PMBUS_H_
#define PMBUS_PMBUS_H_

#define PMBUS_PAGE                0x00
#define PMBUS_OPERATION           0x01
#define PMBUS_ON_OFF_CONFIG       0x02
#define PMBUS_CLEAR_FAULTS        0x03
#define PMBUS_WRITE_PROTECT       0x10
#define PMBUS_STORE_DEFAULT_ALL   0x11
#define PMBUS_RESTORE_DEFAULT_ALL 0x12
#define PMBUS_CAPABILITY          0x19
#define PMBUS_QUERY               0x1A
#define PMBUS_SMBALERT_MASK       0x1B
#define PMBUS_VOUT_MODE           0x20
#define PMBUS_VOUT_COMMAND        0x21
#define PMBUS_VOUT_MAX            0x24
#define PMBUS_VOUT_MARGIN_HIGH    0x25
#define PMBUS_VOUT_MARGIN_LOW     0x26
#define PMBUS_VOUT_MIN            0x2B
#define PMBUS_VIN_ON              0x35
#define PMBUS_VIN_OFF             0x36
#define PMBUS_FAN_CONFIG_12       0x3A
#define PMBUS_FAN_COMMAND_1       0x3B
#define PMBUS_VOUT_OV_FAULT_LIMIT 0x40
#define PMBUS_VOUT_OV_FAULT_RESPONSE 0x41
#define PMBUS_VOUT_OV_WARN_LIMIT  0x42
#define PMBUS_VOUT_UV_WARN_LIMIT  0x43
#define PMBUS_VOUT_UV_FAULT_LIMIT 0x44
#define PMBUS_VOUT_UV_FAULT_RESPONSE 0x45
#define PMBUS_IOUT_OC_FAULT_LIMIT 0x46
#define PMBUS_IOUT_OC_FAULT_RESPONSE 0x47
#define PMBUS_IOUT_OC_LV_FAULT_LIMIT 0x48
#define PMBUS_IOUT_OC_LV_FAULT_RESPONSE 0x49
#define PMBUS_IOUT_OC_WARN_LIMIT  0x4A
#define PMBUS_OT_FAULT_LIMIT      0x4F
#define PMBUS_OT_FAULT_RESPONSE   0x50
#define PMBUS_OT_WARN_LIMIT       0x51
#define PMBUS_UT_WARN_LIMIT       0x52
#define PMBUS_UT_FAULT_LIMIT      0x53
#define PMBUS_UT_FAULT_RESPONSE   0x54
#define PMBUS_VIN_OV_FAULT_LIMIT  0x55
#define PMBUS_VIN_OV_FAULT_RESPONSE 0x56
#define PMBUS_VIN_OV_WARN_LIMIT   0x57
#define PMBUS_VIN_UV_WARN_LIMIT   0x58
#define PMBUS_VIN_UV_FAULT_LIMIT  0x59
#define PMBUS_VIN_UV_FAULT_RESPONSE 0x5A
#define PMBUS_IIN_OC_FAULT_LIMIT  0x5B
#define PMBUS_IIN_OC_FAULT_RESPONSE 0x5C
#define PMBUS_IIN_OC_WARN_LIMIT   0x5D
#define PMBUS_POWER_GOOD_ON       0x5E
#define PMBUS_POWER_GOOD_OFF      0x5F
#define PMBUS_TON_DELAY           0x60
#define PMBUS_TON_RISE            0x61
#define PMBUS_TON_MAX_FAULT_LIMIT 0x62
#define PMBUS_TON_MAX_FAULT_RESPONSE 0x63
#define PMBUS_TOFF_DELAY          0x64
#define PMBUS_TOFF_FALL           0x65
#define PMBUS_TOFF_MAX_WARN_LIMIT 0x66
#define PMBUS_POUT_OP_FAULT_LIMIT 0x68
#define PMBUS_POUT_OP_FAULT_RESPONSE 0x69
#define PMBUS_POUT_OP_WARN_LIMIT  0x6A
#define PMBUS_PIN_OP_WARN_LIMIT   0x6B
#define PMBUS_STATUS_BYTE         0x78
#define PMBUS_STATUS_WORD         0x79
#define PMBUS_STATUS_VOUT         0x7A
#define PMBUS_STATUS_IOUT         0x7B
#define PMBUS_STATUS_INPUT        0x7C
#define PMBUS_STATUS_TEMPERATURE  0x7D
#define PMBUS_STATUS_CML          0x7E
#define PMBUS_STATUS_OTHER        0x7F
#define PMBUS_STATUS_MFR_SPECIFIC 0x80
#define PMBUS_STATUS_FANS_1_2     0x81
#define PMBUS_READ_VIN            0x88
#define PMBUS_READ_IIN            0x89
#define PMBUS_READ_VOUT           0x8B
#define PMBUS_READ_IOUT           0x8C
#define PMBUS_READ_TEMPERATURE_1  0x8D
#define PMBUS_READ_TEMPERATURE_2  0x8E
#define PMBUS_READ_FAN_SPEED_1    0x90
#define PMBUS_READ_POUT           0x96
#define PMBUS_READ_PIN            0x97
#define PMBUS_PMBUS_REVISION      0x98
#define PMBUS_MFR_ID              0x99
#define PMBUS_MFR_MODEL           0x9A
#define PMBUS_MFR_REVISION        0x9B
#define PMBUS_MFR_LOCATION        0x9C
#define PMBUS_MFR_DATE            0x9D
#define PMBUS_MFR_SERIAL          0x9E
#define PMBUS_MFR_VIN_MIN         0xA0
#define PMBUS_MFR_VIN_MAX         0xA1
#define PMBUS_MFR_IIN_MAX         0xA2
#define PMBUS_MFR_PIN_MAX         0xA3
#define PMBUS_MFR_VOUT_MIN        0xA4
#define PMBUS_MFR_VOUT_MAX        0xA5
#define PMBUS_MFR_IOUT_MAX        0xA6
#define PMBUS_MFR_POUT_MAX        0xA7
#define PMBUS_IC_DEVICE_ID        0xAD
#define PMBUS_IC_DEVICE_REV       0xAE
#define PMBUS_USER_DATA_00        0xB0
#define PMBUS_USER_DATA_15        0xBF
#define PMBUS_MFR_MAX_TEMP_1      0xC0
#define PMBUS_MFR_MAX_TEMP_2      0xC1
#define PMBUS_MFR_SPECIFIC_00     0xD0
#define PMBUS_MFR_SPECIFIC_01     0xD1
#define PMBUS_MFR_SPECIFIC_02     0xD2
#define PMBUS_MFR_SPECIFIC_03     0xD3

/* OPERATION */
#define PMBUS_OPERATION_ON          0x80
#define PMBUS_OPERATION_MARGIN_MASK 0x30
#define PMBUS_OPERATION_MARGIN_LOW  0x10
#define PMBUS_OPERATION_MARGIN_HIGH 0x20
#define PMBUS_OPERATION_SOFT_OFF    0x40 /* with ON clear: off through TOFF_DELAY/TOFF_FALL */
#define PMBUS_OPERATION_MFAULT_MASK 0x0C
#define PMBUS_OPERATION_MFAULT_IGNORE 0x04 /* while margined, ignore VOUT OV/UV */

/* ON_OFF_CONFIG */
#define PMBUS_ON_OFF_PU  0x10 /* unit needs a command or the CONTROL pin to turn on */
#define PMBUS_ON_OFF_CMD 0x08 /* unit obeys the OPERATION on bit */
#define PMBUS_ON_OFF_CPR 0x04 /* unit needs the CONTROL pin */
#define PMBUS_ON_OFF_CPA 0x01 /* CONTROL off: 1 = immediately, 0 = TOFF_DELAY/TOFF_FALL */

/*
 * Fault response bytes (Part II section 10.5). Bits 7:6 the response, 5:3 the retry
 * setting, 2:0 a delay in device-defined units.
 */
#define PMBUS_RSP(b)         (((b) >> 6) & 3)
#define PMBUS_RSP_RETRY(b)   (((b) >> 3) & 7)
#define PMBUS_RSP_DELAY(b)   ((b) & 7)
#define PMBUS_RSP_CONTINUE   0 /* keep running */
#define PMBUS_RSP_DELAYED    1 /* keep running for the delay, then shut down and retry */
#define PMBUS_RSP_SHUTDOWN   2 /* shut down, then retry as bits 5:3 say */
#define PMBUS_RSP_INHIBIT    3 /* off while the fault is present */
/* IOUT_OC_FAULT_RESPONSE has its own meanings for bits 7:6. */
#define PMBUS_RSP_IOC_CC     0 /* constant current, indefinitely */
#define PMBUS_RSP_IOC_CC_LV  1 /* constant current while VOUT > IOUT_OC_LV_FAULT_LIMIT */
#define PMBUS_RSP_IOC_CC_DLY 2 /* constant current for the delay, then shut down */
#define PMBUS_RSP_IOC_OFF    3 /* shut down */
#define PMBUS_RETRY_NONE     0 /* stay off until re-enabled */
#define PMBUS_RETRY_FOREVER  7

/* FAN_CONFIG_1_2 */
#define PMBUS_FAN1_INSTALLED 0x80
#define PMBUS_FAN1_RPM       0x40 /* FAN_COMMAND_1 in RPM, else in percent duty */
#define PMBUS_FAN1_PULSES    0x30

/* WRITE_PROTECT */
#define PMBUS_WP_ALL      0x80 /* everything except WRITE_PROTECT */
#define PMBUS_WP_OP       0x40 /* everything except WRITE_PROTECT, OPERATION, PAGE */
#define PMBUS_WP_VOUT_CMD 0x20 /* ...also except ON_OFF_CONFIG and VOUT_COMMAND */

/* CAPABILITY */
#define PMBUS_CAP_PEC      0x80
#define PMBUS_CAP_400KHZ   0x20
#define PMBUS_CAP_SMBALERT 0x10

/* QUERY response */
#define PMBUS_QUERY_SUPPORTED   0x80
#define PMBUS_QUERY_WRITE       0x40
#define PMBUS_QUERY_READ        0x20
#define PMBUS_QUERY_FMT_LINEAR  (0 << 2)
#define PMBUS_QUERY_FMT_U8      (4 << 2)
#define PMBUS_QUERY_FMT_NONNUM  (7 << 2)

/* STATUS_BYTE, the low byte of STATUS_WORD */
#define PMBUS_SB_BUSY           0x80
#define PMBUS_SB_OFF            0x40
#define PMBUS_SB_VOUT_OV_FAULT  0x20
#define PMBUS_SB_IOUT_OC_FAULT  0x10
#define PMBUS_SB_VIN_UV_FAULT   0x08
#define PMBUS_SB_TEMPERATURE    0x04
#define PMBUS_SB_CML            0x02
#define PMBUS_SB_NONE_OF_ABOVE  0x01

/* The high byte of STATUS_WORD */
#define PMBUS_SW_VOUT           0x8000
#define PMBUS_SW_IOUT_POUT      0x4000
#define PMBUS_SW_INPUT          0x2000
#define PMBUS_SW_MFR            0x1000
#define PMBUS_SW_POWER_GOOD_N   0x0800
#define PMBUS_SW_FANS           0x0400
#define PMBUS_SW_OTHER          0x0200

/* STATUS_VOUT */
#define PMBUS_SV_OV_FAULT 0x80
#define PMBUS_SV_OV_WARN  0x40
#define PMBUS_SV_UV_WARN  0x20
#define PMBUS_SV_UV_FAULT 0x10
#define PMBUS_SV_MAX_MIN_WARN 0x08 /* a set point beyond VOUT_MAX or VOUT_MIN */
#define PMBUS_SV_TON_MAX_FAULT 0x04
#define PMBUS_SV_TOFF_MAX_WARN 0x02

/* STATUS_IOUT */
#define PMBUS_SI_OC_FAULT 0x80
#define PMBUS_SI_OC_LV_FAULT 0x40
#define PMBUS_SI_OC_WARN  0x20
#define PMBUS_SI_POUT_OP_FAULT 0x02
#define PMBUS_SI_POUT_OP_WARN  0x01

/* STATUS_INPUT */
#define PMBUS_SIN_OV_FAULT   0x80
#define PMBUS_SIN_OV_WARN    0x40
#define PMBUS_SIN_UV_WARN    0x20
#define PMBUS_SIN_UV_FAULT   0x10
#define PMBUS_SIN_OFF_LOW_IN 0x08
#define PMBUS_SIN_IIN_OC_FAULT 0x04
#define PMBUS_SIN_IIN_OC_WARN  0x02
#define PMBUS_SIN_PIN_OP_WARN  0x01

/* STATUS_TEMPERATURE */
#define PMBUS_ST_OT_FAULT 0x80
#define PMBUS_ST_OT_WARN  0x40
#define PMBUS_ST_UT_WARN  0x20
#define PMBUS_ST_UT_FAULT 0x10

/* STATUS_FANS_1_2 */
#define PMBUS_SF_FAN1_FAULT 0x80
#define PMBUS_SF_FAN1_WARN  0x20

/* STATUS_CML */
#define PMBUS_CML_INVALID_CMD  0x80
#define PMBUS_CML_INVALID_DATA 0x40
#define PMBUS_CML_PEC_FAILED   0x20
#define PMBUS_CML_MEMORY_FAULT 0x10
#define PMBUS_CML_OTHER_COMM   0x02

/* PMBUS_REVISION: Part I and Part II revision 1.3 */
#define PMBUS_REVISION_1_3 0x33

/* The SMBus Alert Response Address. */
#define SMBUS_ALERT_RESPONSE_ADDR 0x0C

#endif /* PMBUS_PMBUS_H_ */
