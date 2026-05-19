#ifndef UART_CMD_H
#define UART_CMD_H

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

void uart_cmd_buffer_init(const struct device *uart_dev);
void uart_cmd_receive_char(uint8_t c);
void uart_cmd_process(void);
void uart30_send(const char *data, const char *subject);


#ifdef __cplusplus
}
#endif

#endif // UART_CMD_H
