/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Model handler
 */

#ifndef MODEL_HANDLER_H__
#define MODEL_HANDLER_H__

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct bt_mesh_comp *model_handler_init(void);
int model_handler_onoff_set(uint16_t net_idx, uint16_t app_idx, uint16_t dst_addr, bool on_off);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_HANDLER_H__ */
