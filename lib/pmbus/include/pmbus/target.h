/*
 * SMBus/PMBus target protocol engine.
 *
 * The engine turns the byte-level events an I2C target driver reports (start, byte
 * written, byte read, stop) into PMBus transactions: it frames Send Byte, Write/Read
 * Byte, Write/Read Word, Block Write/Read and Block Write-Block Read Process Call,
 * checks and appends the packet error code (PEC), and answers the SMBus Alert Response
 * Address. What the commands mean is up to a command table that the caller supplies.
 *
 * Writes take effect at STOP, so a PMBus Group Command, in which several devices
 * receive commands separated by repeated starts, executes at the same moment on every
 * device.
 *
 * The engine has no OS dependencies. Feed it from one context only (the I2C interrupt
 * on the target, the test harness on a host).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PMBUS_TARGET_H_
#define PMBUS_TARGET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The largest SMBus 2.0 block, which is what PMBus 1.3 devices use. */
#define PMBUS_BLOCK_MAX 32

enum pmbus_cmd_type {
	PMBUS_T_SEND_BYTE,  /* command code only */
	PMBUS_T_BYTE,       /* Read/Write Byte */
	PMBUS_T_WORD,       /* Read/Write Word, little-endian on the wire */
	PMBUS_T_BLOCK,      /* Block Read/Write, byte count first */
	PMBUS_T_BLOCK_PROC, /* Block Write-Block Read Process Call */
	/*
	 * Written with Write Word, read with a Block Write-Block Read Process Call whose
	 * written byte selects what to read (SMBALERT_MASK).
	 */
	PMBUS_T_WORD_PROC,
};

#define PMBUS_A_R 0x1
#define PMBUS_A_W 0x2

struct pmbus_cmd {
	uint8_t code;
	uint8_t type;   /* enum pmbus_cmd_type */
	uint8_t access; /* PMBUS_A_R | PMBUS_A_W */
	uint8_t query;  /* the format bits of a QUERY response (PMBUS_QUERY_FMT_*) */
	/*
	 * Fills buf with the data bytes, without the block byte count, and returns how many
	 * there are, or a negative value if the read fails. For a process call, in and
	 * in_len hold the data the host wrote. For other types they are NULL and 0.
	 */
	int (*read)(void *ctx, uint8_t code, const uint8_t *in, size_t in_len, uint8_t *buf,
		    size_t max);
	/* Takes the data bytes, without a block byte count. Returns 0 or a negative value. */
	int (*write)(void *ctx, uint8_t code, const uint8_t *data, size_t len);
};

struct pmbus_target_ops {
	/* Returns the command, or NULL if this device doesn't support it right now. */
	const struct pmbus_cmd *(*lookup)(void *ctx, uint8_t code);
	/* Says whether WRITE_PROTECT currently allows writing this command. */
	bool (*write_allowed)(void *ctx, uint8_t code);
	/* Reports a communication error: PMBUS_CML_* bits for STATUS_CML. */
	void (*cml_error)(void *ctx, uint8_t cml_bits);
	/*
	 * Called when the host reads the Alert Response Address. Returns true, and releases
	 * SMBALERT#, if this device has an alert to report.
	 */
	bool (*alert_response)(void *ctx);
};

enum pmbus_target_state {
	PMBUS_TS_IDLE,
	PMBUS_TS_WRITE,
	PMBUS_TS_READ,
};

struct pmbus_target {
	uint8_t addr;
	const struct pmbus_target_ops *ops;
	void *ctx;

	enum pmbus_target_state state;
	bool read_seen;   /* a read phase happened in this transaction */
	bool rx_overflow; /* the host wrote more than fits */
	uint8_t rx[3 + PMBUS_BLOCK_MAX + 1]; /* command, count, data, PEC */
	uint8_t rx_len;
	uint8_t tx[1 + PMBUS_BLOCK_MAX + 1]; /* count, data, PEC */
	uint8_t tx_len;
	uint8_t tx_pos;

	/* Counters, for diagnostics. */
	uint32_t n_transactions;
	uint32_t n_pec_errors;
	uint32_t n_aborts;
};

void pmbus_target_init(struct pmbus_target *t, uint8_t addr, const struct pmbus_target_ops *ops,
		       void *ctx);

/* START or repeated START with the target address and the write bit. */
void pmbus_target_write_start(struct pmbus_target *t);
/* A byte the host wrote. Returns true to ACK it, false to NACK it. */
bool pmbus_target_write_byte(struct pmbus_target *t, uint8_t byte);
/* START or repeated START with the read bit. Returns the first byte to send. */
uint8_t pmbus_target_read_start(struct pmbus_target *t);
/*
 * START with the read bit to the Alert Response Address (0x0C) instead of the device's
 * own address. Returns the first byte to send: the device address in bits 7:1 if it was
 * alerting, else 0xFF. The I2C driver has to tell the two addresses apart.
 */
uint8_t pmbus_target_ara_start(struct pmbus_target *t);
/* Returns the next byte to send. Drivers may ask for one byte more than the host reads. */
uint8_t pmbus_target_read_byte(struct pmbus_target *t);
/* STOP. Executes a pending write. */
void pmbus_target_stop(struct pmbus_target *t);
/* Drops the transaction in progress, e.g. after an SMBus timeout. */
void pmbus_target_abort(struct pmbus_target *t);

#endif /* PMBUS_TARGET_H_ */
