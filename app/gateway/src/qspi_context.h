/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private API for SPI drivers
 */

#ifndef ZEPHYR_DRIVERS_SPI_SPI_CONTEXT_H_
#define ZEPHYR_DRIVERS_SPI_SPI_CONTEXT_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct qspi_sam0_config;

struct qspi_context {
	const struct qspi_sam0_config *config;
#ifdef CONFIG_MULTITHREADING
	const struct qspi_sam0_config *owner;
#endif

#ifdef CONFIG_MULTITHREADING
	struct k_sem lock;
	struct k_sem sync;
	struct k_mutex psram_mem;
#else
	/* An atomic flag that signals completed transfer
	 * when threads are not enabled.
	 */
	atomic_t ready;
#endif /* CONFIG_MULTITHREADING */
	int sync_status;
};

#define QSPI_CONTEXT_INIT_LOCK(_data, _ctx_name)                                                   \
	._ctx_name.lock = Z_SEM_INITIALIZER(_data._ctx_name.lock, 0, 1)

#define QSPI_CONTEXT_INIT_SYNC(_data, _ctx_name)                                                   \
	._ctx_name.sync = Z_SEM_INITIALIZER(_data._ctx_name.sync, 0, 1)

#define QSPI_CONTEXT_CS_GPIOS_INITIALIZE(...)

/*
 * Checks if a spi config is the same as the one stored in the spi_context
 * The intention of this function is to be used to check if a driver can skip
 * some reconfiguration for a transfer in a fast code path.
 */
static inline bool qspi_context_configured(struct qspi_context *ctx,
					   const struct qspi_sam0_config *config)
{
	return !!(ctx->config == config);
}

/*
 * The purpose of the context lock is to synchronize the usage of the driver/hardware.
 * The driver should call this function to claim or wait for ownership of the spi resource.
 * Usually the appropriate time to call this is at the start of the transceive API implementation.
 */
static inline void qspi_context_lock(struct qspi_context *ctx, bool asynchronous,
				     spi_callback_t callback, void *callback_data,
				     const struct qspi_sam0_config *qspi_cfg)
{
#ifdef CONFIG_MULTITHREADING
	bool already_locked = (k_sem_count_get(&ctx->lock) == 0) && (ctx->owner == qspi_cfg);

	if (!already_locked) {
		k_sem_take(&ctx->lock, K_FOREVER);
		ctx->owner = qspi_cfg;
	}
#endif /* CONFIG_MULTITHREADING */
}

/*
 * This function must be called by a driver which has called spi_context_lock in order
 * to release the ownership of the spi resource.
 * Usually the appropriate time to call this would be at the end of a transfer that was
 * initiated by a transceive API call, except in the case that the SPI_LOCK_ON bit was set
 * in the configuration.
 */
static inline void qspi_context_release(struct qspi_context *ctx, int status)
{
#ifdef CONFIG_MULTITHREADING

	if (ctx->config == NULL) {
		ctx->owner = NULL;
		k_sem_give(&ctx->lock);
	}
#endif /* CONFIG_MULTITHREADING */
}

/* Forcefully releases the spi context and removes the owner, allowing taking the lock
 * with spi_context_lock without the previous owner releasing the lock.
 * This is usually used to aid in implementation of the spi_release driver API.
 */
static inline void qspi_context_unlock_unconditionally(struct qspi_context *ctx __maybe_unused)
{

#ifdef CONFIG_MULTITHREADING
	if (!k_sem_count_get(&ctx->lock)) {
		ctx->owner = NULL;
		k_sem_give(&ctx->lock);
	}
#endif /* CONFIG_MULTITHREADING */
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SPI_SPI_CONTEXT_H_ */
