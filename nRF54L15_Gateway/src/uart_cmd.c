#include "uart_cmd.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <string.h>
#include <stdio.h>
#include "log_capture.h"
#include "model_handler.h"

#include <zephyr/bluetooth/mesh.h>
#if defined(CONFIG_BT_MESH_SHELL)
#include <zephyr/bluetooth/mesh/shell.h>
#endif


#define CMD_BUFFER_SIZE 128
#define CMD_QUEUE_LEN   32
#define RX_CMD_QUEUE_LEN 32
#define ONOFF_STATUS_GROUP_ADDR 0xC000
static const struct device *uart_dev;
K_MSGQ_DEFINE(cmd_msgq, CMD_BUFFER_SIZE, CMD_QUEUE_LEN, 4);
K_MSGQ_DEFINE(rx_cmd_msgq, CMD_BUFFER_SIZE, RX_CMD_QUEUE_LEN, 4);

// Available commands:
// - init: Initialize device and mesh network
// - scan: Toggle scanning for unprovisioned devices
// - provandconfig <uuid> <net_idx> <app_idx>: Provision and configure a device
// - light <net_idx> <app_idx> <dst_addr> <on/off>: Control light on/off
// - cdb: Display the mesh database
// - replace netkey:<netkey> address:<addr>: Initialize device and mesh network with given netkey and address
// - add appkey <app_idx> <appkey>: Add a application key
// - node uuid:<uuid> devkey:<devkey> address:<addr> elements:<elements>: Manually add node to database

static char *uuids_cur_scan[32];
static int num_uuids_cur_scan = 0;

const uint16_t net_idx = 0x0000;
static uint16_t cur_app_idx = 0;
static uint16_t app_idx_list[10] = { [0 ... 9] = 0xFFFF};
static int prov_count = 0x0001;
static uint16_t local_addr;


static void run_command(const char *command);
void uart30_send(const char *data, const char *subject);
static bool enqueue_command(const char *cmd); 

// converts 128-bit uuid received from unprovisioned beacon to readable string format
static void uuid_to_str(const uint8_t uuid[16], char *out, size_t out_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t p = 0;

    for (size_t i = 0; i < 16 && (p + 2) < out_len; i++) {
        out[p++] = hex[(uuid[i] >> 4) & 0x0F];
        out[p++] = hex[uuid[i] & 0x0F];
    }
    if (p < out_len) {
        out[p] = '\0';
    } else if (out_len > 0) {
        out[out_len - 1] = '\0';
    }
}

// Callback for unprovisioned beacons, sends the uuid to the cloud if it's not already sent during current scan session
static void uart_unprov_beacon_cb(const uint8_t uuid[16],
                                  bt_mesh_prov_oob_info_t oob_info,
                                  uint32_t *uri_hash){
    char uuid_str[33];
    char line[128];

    uuid_to_str(uuid, uuid_str, sizeof(uuid_str));

    printk("uuid=%s\r\n", uuid_str);

    for (int i = 0; i < num_uuids_cur_scan; i++) {
        if (strcmp(uuids_cur_scan[i], uuid_str) == 0) {
            return;
        }
    }
    if (num_uuids_cur_scan < 32) {
        uuids_cur_scan[num_uuids_cur_scan++] = strdup(uuid_str);
    }

    snprintf(line, sizeof(line), "%s", uuid_str);
    uart30_send(line, "UUID");
}

// Callback for when a node is provisioned
static void uart_node_added_cb(uint16_t net_idx,
                               const uint8_t uuid[16],
                               uint16_t addr,
                               uint8_t num_elem)
{
    char cmd[CMD_BUFFER_SIZE];
        snprintf(cmd, sizeof(cmd), "mesh target dst 0x%04x", addr);
        enqueue_command(cmd);
        snprintf(cmd, sizeof(cmd), "mesh target net %u", net_idx);
        enqueue_command(cmd);
        snprintf(cmd, sizeof(cmd), "mesh models cfg appkey add %u %u", net_idx, cur_app_idx);
        enqueue_command(cmd);

        // Bind appkey to all onoff server models on the new node and configure publication to shared group address for status updates
        for (int i = 0; i < num_elem; i++) {
            snprintf(cmd, sizeof(cmd), "mesh models cfg model app-bind 0x%04x %u 0x1000", addr + i, cur_app_idx);
            enqueue_command(cmd);
            /* Configure Generic OnOff Server publications to a shared group address. */
            snprintf(cmd, sizeof(cmd), "mesh models cfg model pub 0x%04x 0x1000 0x%04x %u 0 7 0 0 0 0", addr + i, ONOFF_STATUS_GROUP_ADDR, cur_app_idx);
            enqueue_command(cmd);
            prov_count += 1;
        }

        // Notify cloud of new provisioned node
        char response[128];
        snprintf(response, sizeof(response), "Binding complete on app_idx %u and net_idx %u on addresses 0x%04x-0x%04x", cur_app_idx, net_idx, addr, addr+num_elem-1);
        uart30_send(response, "Provisioning");
}

// queues a shell command to be executed by the shell_cmd_executor_thread
static bool enqueue_command(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') {
        return false;
    }

    char tmp[CMD_BUFFER_SIZE];
    snprintf(tmp, sizeof(tmp), "%s", cmd);

    if (k_msgq_put(&cmd_msgq, tmp, K_NO_WAIT) != 0) {
        printk("Command queue full, dropping: %s\n", tmp);
        const char *response = "ERROR: Command queue full";
        uart30_send(response, "Error");
        return false;
    }
    return true;
}

// removes ANSI escape codes and other unwanted characters from the input string
size_t strip_ansi_escapes(const char *src, size_t src_len, char *dst, size_t dst_size) {
    size_t dst_pos = 0;
    size_t i = 0;
    while (i < src_len && dst_pos < dst_size - 1) {
        if (src[i] == 0x1b) {
            i++;
            if (i < src_len && src[i] == '[') {
                i++;
                while (i < src_len && ((src[i] < '@' || src[i] > '~'))) {
                    i++;
                }
                if (i < src_len) i++;
            }
        } else if (src[i] == '~' && i + 2 < src_len && src[i+1] == '$' && src[i+2] == ' ') {
            i += 3;
        } else if (src[i] == '\r') {
            i++;
        } else {
            dst[dst_pos++] = src[i++];
        }
    }
    dst[dst_pos] = '\0';
    return dst_pos;
}

// sends string over uart to nRF9151
void uart30_send(const char *data, const char *subject)
{
    if (!uart_dev || !device_is_ready(uart_dev)) {
        return;
    }
    char json_buf[1024];
    snprintf(json_buf, sizeof(json_buf), "%s %s",subject, data);
    size_t json_len = strip_ansi_escapes(json_buf, strlen(json_buf), json_buf, sizeof(json_buf));
    for (size_t i = 0; i < json_len; i++) {
        uart_poll_out(uart_dev, json_buf[i]);

        // for each new line, send the subject first again
        // nrf9151 expects the first word of a line to be the subject
        if(json_buf[i] == '\n') {
            for (int i = 0; i < strlen(subject); i++) {
                uart_poll_out(uart_dev, subject[i]);

            }
            uart_poll_out(uart_dev, ' ');
        }
    }
    uart_poll_out(uart_dev, '\n');
}

// receives uart commands from uart and queues them for execution
static void uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    static char uart_buffer[CMD_BUFFER_SIZE];
    static size_t uart_buffer_pos = 0;
    uint8_t c;
    if (!uart_irq_update(dev)) {
        return;
    }
    if (!uart_irq_rx_ready(dev)) {
        return;
    }
    while (uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\r' || c == '\n') {
            if (uart_buffer_pos > 0) {
                // queue command and reset buffer
                (void)k_msgq_put(&rx_cmd_msgq, uart_buffer, K_NO_WAIT);
                uart_buffer_pos = 0;
                memset(uart_buffer, 0, sizeof(uart_buffer));
            }else {
                printk("Received empty command, ignoring\n");
            }
        
        } else if (uart_buffer_pos < CMD_BUFFER_SIZE - 1) {
            // add character to buffer
            uart_buffer[uart_buffer_pos++] = (char)c;
        } else {
            printk("Command buffer overflow, resetting\n");
            uart_buffer_pos = 0;
            memset(uart_buffer, 0, sizeof(uart_buffer));
        }
    }
}

// adds an appkey to the mesh network and binds it to the local onoff client model
void add_appkey(uint16_t app_idx, char app_key[32]) {

    enqueue_command("mesh target net 0");
    enqueue_command("mesh target dst local");
    char cmd[96];
    if (app_key && app_key[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "mesh cdb app-key-add %u %u %s", net_idx, app_idx, app_key);
    } else {
        snprintf(cmd, sizeof(cmd), "mesh cdb app-key-add %u %u", net_idx, app_idx);
    }
    enqueue_command(cmd);
    snprintf(cmd, sizeof(cmd), "mesh models cfg appkey add %u %u", net_idx, app_idx);
    enqueue_command(cmd);
    snprintf(cmd, sizeof(cmd), "mesh models cfg model app-bind 0x%04x %u 0x1001", local_addr, app_idx);
    enqueue_command(cmd);
    //add appkey to list of app_idx
    for (int i = 0; i < ARRAY_SIZE(app_idx_list); i++) {
        if (app_idx_list[i] == 0xFFFF) {
            app_idx_list[i] = app_idx;
            break;
        }
    }
    
}

// main command processing thread, compares received command to supported commands and runs the corresponding function
static void run_command(const char *command)
{
    static bool scanning = false;
    printk("received command: %s\n", command);
    if (strcmp(command, "init") == 0) { // Initialize mesh network and provision local device
        printk("Initializing device...\n");
        enqueue_command("mesh init");
        enqueue_command("mesh cdb create");
        app_idx_list[0] = 0;
        enqueue_command("mesh prov local 0 0x0001");
        local_addr = 0x0001;
        prov_count += 1;
        add_appkey(0, NULL); // add default appkey
        // Subscribe local Generic OnOff Client to status publications.
        char cmd[CMD_BUFFER_SIZE];
        snprintf(cmd, sizeof(cmd), "mesh models cfg model sub-add 0x%04x 0x%04x 0x1001", local_addr, ONOFF_STATUS_GROUP_ADDR);
        enqueue_command(cmd);
        
        bt_mesh_shell_prov.node_added = uart_node_added_cb; // activate callback for povisioning a new node
        const char *response = "initialization complete";
        uart30_send(response, "init_complete");
    } else if (strcmp(command, "scan") == 0) { // Toggle scanning for unprovisioned devices
        scanning = !scanning;
        if (scanning) {
            printk("Scanning for devices...\n");
            const char *response = "scan started";
            uart30_send(response, "response");
            num_uuids_cur_scan = 0; // Reset UUID list for new scan
            bt_mesh_shell_prov.unprovisioned_beacon = uart_unprov_beacon_cb; // activate callback for unprovisioned beacons
        } else {
            printk("Stopping scan...\n");
            const char *response = "scan stopped";
            uart30_send(response, "response");
            bt_mesh_shell_prov.unprovisioned_beacon = NULL; // deactivate callback to stop receiving unprovisioned beacons
        }
    } else if (strncmp(command, "provandconfig", strlen("provandconfig")) == 0) { // Provision a device with given uuid and configure it with uart_node_added_cb
        unsigned int net_idx = 0U, app_idx = 0U;
        char uuid[33];
        if (sscanf(command, "provandconfig %32s %u %u", uuid, &net_idx, &app_idx) != 3) {
            printk("wrong formating provandconfig, Usage: provandconfig <uuid> <net_idx> <app_idx>\n");
            char response[128];
            snprintf(response, sizeof(response), "wrong formating provandconfig, Usage: provandconfig <uuid> <net_idx> <app_idx>");
            uart30_send(response, "Error");
            return;
        }
        //check if app_idx exist
        bool app_idx_exists = false;
        for (int i = 0; i < ARRAY_SIZE(app_idx_list); i++) {
            if (app_idx_list[i] == app_idx) {
                app_idx_exists = true;
                break;
            }
        }
        if (!app_idx_exists) {
            printk("AppKey index %u does not exist, creating new", app_idx);
            add_appkey(app_idx, NULL);
        }
        cur_app_idx = app_idx; //used in uart_node_added_cb for binding the correct appkey to the new node
        char cmd[CMD_BUFFER_SIZE];
        snprintf(cmd, sizeof(cmd),"mesh prov remote-adv %s %u 0x%04x 5", uuid, net_idx, prov_count);
        enqueue_command(cmd);

        char response[128];
        snprintf(response, sizeof(response), "Provisioning started");
        uart30_send(response, "response");
    } else if (strncmp(command, "light", strlen("light")) == 0) { // Send on/off command to a light
        unsigned int net_idx = 0U, app_idx = 0U, dst_addr = 0U, on_off = 0U;
        if (sscanf(command, "light %u %u %u %u", &net_idx, &app_idx, &dst_addr, &on_off) != 4) {
            printk("wrong formating light command, Usage: light <net_idx> <app_idx> <dst_addr> <on/off>\n");
            char response[128];
            snprintf(response, sizeof(response), "wrong formating light command, Usage: light <net_idx> <app_idx> <dst_addr> <on/off>");
            uart30_send(response, "Error");
            return;
        }
        int err = model_handler_onoff_set((uint16_t)net_idx,
                                          (uint16_t)app_idx,
                                          (uint16_t)dst_addr,
                                          on_off != 0U);
        char response[128];
        if (err) {
            snprintf(response, sizeof(response), "Light command failed to 0x%04x (err %d)", dst_addr, err);
            uart30_send(response, "Error");
            return;
        }
    }else if (strncmp(command, "cdb", strlen("cdb")) == 0) { // Send configure database to cloud
        enqueue_command("mesh cdb show");
    }else if (strncmp(command, "replace", strlen("replace")) == 0) { // initialize mesh with given netkey and adddress, used for reprovisioning with existing mesh networks
        char netkey[33] = {0};
        if (sscanf(command, "replace netkey:%32s address:%hu", netkey, &local_addr) != 2) {
            printk("wrong formating replace command, Usage: replace netkey:<netkey> address:<addr>\n");
            char response[128];
            snprintf(response, sizeof(response), "wrong formating replace command, Usage: replace netkey:<netkey> address:<addr>");
            uart30_send(response, "Error");
            return;
        }
        prov_count = local_addr + 1;
        char cmd[100];
        enqueue_command("mesh init");
        snprintf(cmd, sizeof(cmd), "mesh cdb create %s", netkey);
        enqueue_command(cmd);
        snprintf(cmd, sizeof(cmd), "mesh prov local %u 0x%04x", net_idx, local_addr);
        enqueue_command(cmd);

        bt_mesh_shell_prov.node_added = uart_node_added_cb; // activate callback for povisioning a new node
        
        // Subscribe local Generic OnOff Client to status publications
        snprintf(cmd, sizeof(cmd), "mesh models cfg model sub-add 0x%04x 0x%04x 0x1001", local_addr, ONOFF_STATUS_GROUP_ADDR);
        enqueue_command(cmd);

        uart30_send("reProvisioned", "reProvisioned");
    }
    else if (strncmp(command, "add appkey", strlen("add appkey")) == 0) { // add an appkey to the mesh network with given app_idx and appkey value
        uint16_t app_idx = 0;
        char appkey[33];
        if (sscanf(command, "add appkey 0x%x %32s", &app_idx, appkey) != 2) {
            printk("wrong formating appkey command, Usage: add appkey <app_idx> <appkey>\n");
            char response[128];
            snprintf(response, sizeof(response), "wrong formating appkey command, Usage: add appkey <app_idx> <appkey>");
            uart30_send(response, "Error");
            return;
        }
        add_appkey(app_idx, appkey);

    }
    else if(strncmp(command, "node", strlen("node")) == 0) { // Manually add a node to the database, used for adding nodes that were provisioned by another provisioner
        char uuid[33] = {0}, devkey[33] = {0};
        uint16_t addr = 0;
        uint8_t elements = 0;
        if(sscanf(command, "node uuid:%32s devkey:%32s address:0x%hx elements:%hhu", uuid, devkey, &addr, &elements) != 4 ) {
            printk("wrong formating node command, Usage: node uuid:<uuid> devkey:<devkey> address:<addr> elements:<elements>\n");
            char response[128];
            snprintf(response, sizeof(response), "wrong formating node command, Usage: node uuid:<uuid> devkey:<devkey> address:<addr> elements:<elements>");
            uart30_send(response, "Error");
            return;
        }
        char cmd[100];
        snprintf(cmd, sizeof(cmd), "mesh cdb node-add %32s 0x%04x %u %u %32s", uuid, addr, elements, net_idx, devkey);
        enqueue_command(cmd);
        char response[128];
        snprintf(response, sizeof(response), "node provisioned:%s", uuid);
        uart30_send(response, response);
    }
    else { // if command is not recognized attempt to run as a shell command
        enqueue_command(command);
        const char *response = "Command ran";
        uart30_send(response, "response");
    }
}

static void rx_cmd_router_thread(void) // thread for retrieving commands from uart and running them
{
    char rx_cmd[CMD_BUFFER_SIZE];

    while (1) {
        k_msgq_get(&rx_cmd_msgq, rx_cmd, K_FOREVER);
        run_command(rx_cmd);
    }
}
K_THREAD_DEFINE(rx_router_tid, 2048, rx_cmd_router_thread, NULL, NULL, NULL, 5, 0, 0);


static void shell_cmd_executor_thread(void) // thread for executing shell commands
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    char local_cmd[CMD_BUFFER_SIZE];
    while (1) {
        k_msgq_get(&cmd_msgq, local_cmd, K_FOREVER);
        printk("Running command: %s\n", local_cmd);

        if (sh) {
            shell_backend_dummy_clear_output(sh);
            int ret = shell_execute_cmd(sh, local_cmd);
            printk("shell_execute_cmd returned: %d\n", ret);
            size_t output_size;
            const char *output = shell_backend_dummy_get_output(sh, &output_size);
            printk("Command output: %.*s", (int)output_size, output);
            if (strcmp(local_cmd, "mesh cdb show") == 0) { // if command is cdb send entire output to cloud
                uart30_send(output, "cdb");
            }
        } else {
            printk("Shell backend not available\n");
        }
    }
}

K_THREAD_DEFINE(shell_cmd_executor_tid, 4096, shell_cmd_executor_thread, NULL, NULL, NULL, 5, 0, 0);

int uart_cmd_init(void)
{
    int err;
    uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart30));
    if (!device_is_ready(uart_dev)) {
        printk("UART command device not ready\n");
        return -ENODEV;
    }
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
