/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/bluetooth/mesh/shell.h>
#include "model_handler.h"
#include "smp_bt.h"

/*ADDED INCLUDES*/
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>
#include <stdio.h>

#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif



#if defined(CONFIG_BT_MESH_DFD_SRV)
static struct bt_mesh_dfd_srv dfd_srv;
#endif

#if defined(CONFIG_BT_MESH_SAR_CFG_CLI)
static struct bt_mesh_sar_cfg_cli sar_cfg_cli;
#endif

#if defined(CONFIG_BT_MESH_PRIV_BEACON_CLI)
static struct bt_mesh_priv_beacon_cli priv_beacon_cli;
#endif

#if defined(CONFIG_BT_MESH_SOL_PDU_RPL_CLI)
static struct bt_mesh_sol_pdu_rpl_cli srpl_cli;
#endif


#if defined(CONFIG_BT_MESH_OD_PRIV_PROXY_CLI)
static struct bt_mesh_od_priv_proxy_cli od_priv_proxy_cli;
#endif

#if defined(CONFIG_BT_MESH_LARGE_COMP_DATA_CLI)
struct bt_mesh_large_comp_data_cli large_comp_data_cli;
#endif

#if defined(CONFIG_BT_MESH_BRG_CFG_CLI)
static struct bt_mesh_brg_cfg_cli brg_cfg_cli;
#endif

/* UART Command Reception */
#define CMD_BUFFER_SIZE 512
static char cmd_buffer[CMD_BUFFER_SIZE];
static int cmd_buffer_pos = 0;
static K_SEM_DEFINE(cmd_sem, 0, 1);

static const struct device *uart_dev;

/* Ring buffer for capturing log messages */
#define LOG_CAPTURE_BUF_SIZE 256
static uint8_t log_capture_buffer[LOG_CAPTURE_BUF_SIZE];
static struct ring_buf log_ringbuf;
static bool log_capture_enabled = false;

/* Strip ANSI escape sequences and shell prompt from a buffer */
static size_t strip_ansi_escapes(const char *src, size_t src_len, char *dst, size_t dst_size)
{
	size_t dst_pos = 0;
	size_t i = 0;

	while (i < src_len && dst_pos < dst_size - 1) {
		/* Check for ESC character (0x1B) */
		if (src[i] == 0x1b) {
			i++;
			if (i < src_len && src[i] == '[') {
				/* CSI sequence: skip until we hit a letter (0x40-0x7E) */
				i++;
				while (i < src_len && (src[i] < 0x40 || src[i] > 0x7E)) {
					i++;
				}
				if (i < src_len) {
					i++; /* Skip the final character */
				}
			}
			/* Other escape types - just skip the ESC */
		} else if (src[i] == '~' && i + 2 < src_len && src[i+1] == '$' && src[i+2] == ' ') {
			/* Skip shell prompt "~$ " */
			i += 3;
		} else if (src[i] == '\r') {
			/* Skip carriage return */
			i++;
		} else {
			dst[dst_pos++] = src[i++];
		}
	}
	dst[dst_pos] = '\0';
	return dst_pos;
}

/* Send string over UART30 (strips ANSI escape codes) */
static void uart30_send(const char *data, size_t len)
{
	if (!uart_dev || !device_is_ready(uart_dev)) {
		return;
	}

	/* Use a static buffer for stripped output */
	static char clean_buf[512];
	size_t clean_len = strip_ansi_escapes(data, len, clean_buf, sizeof(clean_buf));

	for (size_t i = 0; i < clean_len; i++) {
		uart_poll_out(uart_dev, clean_buf[i]);
	}
}

/* Custom log output for UART30 capture */
static int log_capture_output(uint8_t *buf, size_t size, void *ctx)
{
	ARG_UNUSED(ctx);
	if (log_capture_enabled) {
		ring_buf_put(&log_ringbuf, buf, size);
	}
	return size;
}

static uint8_t log_output_buf[128];
LOG_OUTPUT_DEFINE(log_capture_out, log_capture_output, log_output_buf, sizeof(log_output_buf));

static void log_capture_process(const struct log_backend *const backend,
			       union log_msg_generic *msg)
{
	uint32_t flags = LOG_OUTPUT_FLAG_LEVEL;
	log_output_msg_process(&log_capture_out, &msg->log, flags);
}

static void log_capture_dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(cnt);
}

static void log_capture_panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
}

static void log_capture_init(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
}

static const struct log_backend_api log_capture_api = {
	.process = log_capture_process,
	.dropped = log_capture_dropped,
	.panic = log_capture_panic,
	.init = log_capture_init,
};

LOG_BACKEND_DEFINE(log_capture_backend, log_capture_api, true, NULL);


static void uart_isr(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(dev)) {
		return;
	}

	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	/* Read characters */
	while (uart_fifo_read(dev, &c, 1) == 1) {
		//printk("RX: 0x%02X ('%c')\n", c, (c >= 32 && c < 127) ? c : '.');  // Debug print
		if (c == '\r' || c == '\n') {
			if (cmd_buffer_pos > 0) {
				/* Null-terminate the command */
				cmd_buffer[cmd_buffer_pos] = '\0';
				printk("Command complete: '%s' (length: %d)\n", cmd_buffer, cmd_buffer_pos);
				/* Reset position BEFORE giving semaphore to prevent \r\n race */
				cmd_buffer_pos = 0;
				/* Signal that a command is ready */
				k_sem_give(&cmd_sem);
			}
		} else if (cmd_buffer_pos < CMD_BUFFER_SIZE - 1) {
			/* Add character to buffer */
			cmd_buffer[cmd_buffer_pos++] = c;
		} else {
			/* Buffer full, reset */
			printk("Command buffer overflow, resetting\n");
			cmd_buffer_pos = 0;
		}
	}
}

static void cmd_executor_thread(void)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	char local_cmd[CMD_BUFFER_SIZE];
	size_t output_size;
	const char *output;
	int ret;
	uint8_t log_data[256];
	uint32_t log_size;
	
	/* Initialize ring buffer for log capture */
	ring_buf_init(&log_ringbuf, sizeof(log_capture_buffer), log_capture_buffer);
	
	/* Wait for shell to be ready */
	k_sleep(K_SECONDS(1));
	
	printk("cmd_executor_thread: dummy shell ptr = %p\n", sh);
	
	while (1) {
		/* Wait for a command to be received */
		k_sem_take(&cmd_sem, K_FOREVER);
		
		/* Copy command to local buffer to avoid race with ISR */
		strncpy(local_cmd, cmd_buffer, CMD_BUFFER_SIZE - 1);
		local_cmd[CMD_BUFFER_SIZE - 1] = '\0';
		
		/* Clear shared buffer for next command */
		memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
		
		printk("Executing UART command: %s\n", local_cmd);
		
		/* Execute the command through the dummy shell backend */
		if (sh) {
			/* Clear any previous output and enable log capture */
			shell_backend_dummy_clear_output(sh);
			ring_buf_reset(&log_ringbuf);
			log_capture_enabled = true;
			
			/* Execute command - output goes to dummy backend buffer */
			ret = shell_execute_cmd(sh, local_cmd);
			
			/* Wait briefly for async log messages to arrive */
			k_sleep(K_MSEC(100));
			log_capture_enabled = false;
			
			printk("shell_execute_cmd returned: %d\n", ret);
			
			/* Get the captured shell output */
			output = shell_backend_dummy_get_output(sh, &output_size);
			printk("Shell output size: %d\n", (int)output_size);
			
			/* Send shell output back over UART30 */
			if (output_size > 0) {
				uart30_send(output, output_size);
				uart30_send("\r\n", 2);
			} else {
				/* Only send log messages if there was NO shell output */
				log_size = ring_buf_get(&log_ringbuf, log_data, sizeof(log_data) - 1);
				if (log_size > 0) {
					while (log_size > 0) {
						log_data[log_size] = '\0';
						uart30_send((char *)log_data, log_size);
						log_size = ring_buf_get(&log_ringbuf, log_data, sizeof(log_data) - 1);
					}
				} else {
					/* No shell output and no logs - send return code */
					char resp[64];
					snprintf(resp, sizeof(resp), "ret=%d\r\n", ret);
					uart30_send(resp, strlen(resp));
				}
			}
		} else {
			printk("Shell backend not available\n");
			uart30_send("ERROR: Shell not available\r\n", 28);
		}
	}
}

K_THREAD_DEFINE(cmd_executor_tid, 2048, cmd_executor_thread, NULL, NULL, NULL, 5, 0, 0);


static int uart_cmd_init(void)
{
	int err;

	/* Use UART30 for command reception, not the console UART20 */
	uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart30));
	
	if (!device_is_ready(uart_dev)) {
		printk("UART command device not ready\n");
		return -ENODEV;
	}

	/* Configure interrupt and callback to receive data */
	err = uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
	if (err < 0) {
		if (err == -ENOTSUP) {
			printk("Interrupt-driven UART API not enabled\n");
		}
		return err;
	}

	uart_irq_rx_enable(uart_dev);
	
	printk("UART command reception initialized on UART30\n");
	printk("TX: P0.0, RX: P0.1, 115200 baud\n");

	
	return 0;
}

/* Shell command to test UART30 transmission */
static int cmd_uart_test(const struct shell *sh, size_t argc, char *argv[])
{
	if (!uart_dev || !device_is_ready(uart_dev)) {
		shell_print(sh, "UART30 not ready");
		return -ENODEV;
	}

	const char *msg = (argc > 1) ? argv[1] : "Test message from shell";
	
	shell_print(sh, "Sending to UART30: %s", msg);
	
	for (int i = 0; msg[i] != '\0'; i++) {
		uart_poll_out(uart_dev, msg[i]);
	}
	uart_poll_out(uart_dev, '\r');
	uart_poll_out(uart_dev, '\n');
	
	return 0;
}

SHELL_CMD_REGISTER(uart_test, NULL, "Send test message on UART30", cmd_uart_test);


static void bt_ready(int err)
{
	if (err && err != -EALREADY) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");


	err = bt_mesh_init(&bt_mesh_shell_prov, model_handler_init());
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}


	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	if (bt_mesh_is_provisioned()) {
		printk("Mesh network restored from flash\n");
	} else {
		printk("Use \"prov pb-adv on\" or \"prov pb-gatt on\" to "
			    "enable advertising\n");
	}
}

int main(void)
{
	int err;

	printk("Initializing...\n");

	/* Initialize UART command reception */
	err = uart_cmd_init();
	if (err) {
		printk("UART command init failed (err %d)\n", err);
	}

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err && err != -EALREADY) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	printk("Press the <Tab> button for supported commands.\n");
	printk("Before any Mesh commands you must run \"mesh init\"\n");
	printk("Ready to receive commands via UART...\n");
	
	// Keep main thread alive (don't return)
	while (1) {
		k_sleep(K_FOREVER);
	}
	
	return 0;
}
