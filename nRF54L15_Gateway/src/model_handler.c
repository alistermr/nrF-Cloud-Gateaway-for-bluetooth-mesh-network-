#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/gen_onoff_cli.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/sys/printk.h>
#include "uart_cmd.h"

/*ADDED INCLUDES*/
#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/mesh/shell.h>

static struct bt_mesh_cfg_cli cfg_cli;

// callback for receiving on/off status messages from the mesh network, sends status to cloud
static void OnOff_status_cb(struct bt_mesh_onoff_cli *cli,
                            struct bt_mesh_msg_ctx *ctx,
                            const struct bt_mesh_onoff_status *status)
{
	(void)cli;

	if (!ctx || !status) {
		printk("OnOff status callback received invalid data\n");
		return;
	}

	printk("OnOff status from 0x%04x: status=%u",
	       ctx->addr,
	       status->present_on_off ? 1U : 0U);

	if (status->remaining_time >= 0) {
		printk(", target=%u, remaining=%ld ms",
		       status->target_on_off ? 1U : 0U,
		       (long)status->remaining_time);
	}

	printk("\n");

	char status_msg[96];
	snprintf(status_msg, sizeof(status_msg),
	         "src=0x%04x status=%s",
	         ctx->addr,
	         status->present_on_off ? "ON" : "OFF");
	uart30_send(status_msg, "OnOff_status");
}

static struct bt_mesh_onoff_cli onoff_cli = BT_MESH_ONOFF_CLI_INIT(OnOff_status_cb);

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

BT_MESH_SHELL_HEALTH_PUB_DEFINE(health_pub);

static const struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_ONOFF_CLI(&onoff_cli),
	BT_MESH_MODEL_HEALTH_SRV(&bt_mesh_shell_health_srv, &health_pub, health_srv_meta),
	BT_MESH_MODEL_HEALTH_CLI(&bt_mesh_shell_health_cli),
#if defined(CONFIG_BT_MESH_DFD_SRV)
	BT_MESH_MODEL_DFD_SRV(&dfd_srv),
#else
#if defined(CONFIG_BT_MESH_SHELL_DFU_SRV)
	BT_MESH_MODEL_DFU_SRV(&bt_mesh_shell_dfu_srv),
#elif defined(CONFIG_BT_MESH_SHELL_BLOB_SRV)
	BT_MESH_MODEL_BLOB_SRV(&bt_mesh_shell_blob_srv),
#endif
#if defined(CONFIG_BT_MESH_SHELL_DFU_CLI)
	BT_MESH_MODEL_DFU_CLI(&bt_mesh_shell_dfu_cli),
#elif defined(CONFIG_BT_MESH_SHELL_BLOB_CLI)
	BT_MESH_MODEL_BLOB_CLI(&bt_mesh_shell_blob_cli),
#endif
#endif /* CONFIG_BT_MESH_DFD_SRV */
#if defined(CONFIG_BT_MESH_SHELL_RPR_CLI)
	BT_MESH_MODEL_RPR_CLI(&bt_mesh_shell_rpr_cli),
#endif
#if defined(CONFIG_BT_MESH_RPR_SRV)
	BT_MESH_MODEL_RPR_SRV,
#endif

#if defined(CONFIG_BT_MESH_SAR_CFG_SRV)
	BT_MESH_MODEL_SAR_CFG_SRV,
#endif
#if defined(CONFIG_BT_MESH_SAR_CFG_CLI)
	BT_MESH_MODEL_SAR_CFG_CLI(&sar_cfg_cli),
#endif

#if defined(CONFIG_BT_MESH_OP_AGG_SRV)
	BT_MESH_MODEL_OP_AGG_SRV,
#endif
#if defined(CONFIG_BT_MESH_OP_AGG_CLI)
	BT_MESH_MODEL_OP_AGG_CLI,
#endif

#if defined(CONFIG_BT_MESH_LARGE_COMP_DATA_SRV)
	BT_MESH_MODEL_LARGE_COMP_DATA_SRV,
#endif
#if defined(CONFIG_BT_MESH_LARGE_COMP_DATA_CLI)
	BT_MESH_MODEL_LARGE_COMP_DATA_CLI(&large_comp_data_cli),
#endif

#if defined(CONFIG_BT_MESH_PRIV_BEACON_SRV)
	BT_MESH_MODEL_PRIV_BEACON_SRV,
#endif
#if defined(CONFIG_BT_MESH_PRIV_BEACON_CLI)
	BT_MESH_MODEL_PRIV_BEACON_CLI(&priv_beacon_cli),
#endif
#if defined(CONFIG_BT_MESH_OD_PRIV_PROXY_CLI)
	BT_MESH_MODEL_OD_PRIV_PROXY_CLI(&od_priv_proxy_cli),
#endif
#if defined(CONFIG_BT_MESH_SOL_PDU_RPL_CLI)
	BT_MESH_MODEL_SOL_PDU_RPL_CLI(&srpl_cli),
#endif
#if defined(CONFIG_BT_MESH_OD_PRIV_PROXY_SRV)
	BT_MESH_MODEL_OD_PRIV_PROXY_SRV,
#endif
#if defined(CONFIG_BT_MESH_BRG_CFG_SRV)
	BT_MESH_MODEL_BRG_CFG_SRV,
#endif
#if defined(CONFIG_BT_MESH_BRG_CFG_CLI)
	BT_MESH_MODEL_BRG_CFG_CLI(&brg_cfg_cli),
#endif
};

static const struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};


static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	return &comp;
}

// creates and send a mesh message setting the on/off state of a light
int model_handler_onoff_set(uint16_t net_idx, uint16_t app_idx, uint16_t dst_addr, bool on_off)
{
	struct bt_mesh_msg_ctx ctx = {
		.net_idx = net_idx,
		.app_idx = app_idx,
		.addr = dst_addr,
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};
	struct bt_mesh_onoff_set set = {
		.on_off = on_off,
		.reuse_transaction = false,
		.transition = NULL,
	};

	if (!bt_mesh_is_provisioned()) {
		return -EAGAIN;
	}

	return bt_mesh_onoff_cli_set_unack(&onoff_cli, &ctx, &set);
}
