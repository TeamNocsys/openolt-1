/*
    Copyright (C) 2018 Open Networking Foundation

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <memory>
#include <string>

#include "Queue.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

#include "core.h"
#include "indications.h"
#include "stats_collection.h"
#include "error_format.h"
#include "state.h"

extern "C"
{
#include <bcmos_system.h>
#include <bal_api.h>
#include <bal_api_end.h>
// FIXME : dependency problem
// #include <bcm_common_gpon.h>
}

#define NUM_OF_PON_PORTS 16
const std::string technology = "xgspon";
const std::string firmware_version = "BAL.2.6.0.1__Openolt.2018.09.05";

State state;

static Status SchedAdd_(int intf_id, int onu_id, int agg_port_id, int sched_id, int pir);
static Status SchedRemove_(int intf_id, int onu_id, int agg_port_id, int sched_id);

static inline int mk_sched_id(int intf_id, int onu_id) {
    return 1023 + intf_id * 112 + onu_id;
}

static inline int mk_agg_port_id(int intf_id, int onu_id) {
    return 1023 + intf_id * 112 + onu_id;
}


Status GetDeviceInfo_(openolt::DeviceInfo* device_info) {

    device_info->set_vendor("EdgeCore");
    device_info->set_model("asfvolt16");
    device_info->set_hardware_version("");
    device_info->set_firmware_version(firmware_version);
    device_info->set_technology(technology);
    device_info->set_pon_ports(NUM_OF_PON_PORTS);
    device_info->set_onu_id_start(1);
    device_info->set_onu_id_end(257);
    device_info->set_alloc_id_start(1024);
    device_info->set_alloc_id_end(16383);
    device_info->set_gemport_id_start(1024);
    device_info->set_gemport_id_end(65535);

    // FIXME: Once dependency problem is fixed
    // device_info->set_pon_ports(NUM_OF_PON_PORTS);
    // device_info->set_onu_id_end(XGPON_NUM_OF_ONUS - 1);
    // device_info->set_alloc_id_start(1024);
    // device_info->set_alloc_id_end(XGPON_NUM_OF_ALLOC_IDS * NUM_OF_PON_PORTS ? - 1);
    // device_info->set_gemport_id_start(XGPON_MIN_BASE_SERVICE_PORT_ID);
    // device_info->set_gemport_id_end(XGPON_NUM_OF_GEM_PORT_IDS_PER_PON * NUM_OF_PON_PORTS ? - 1);
    // device_info->set_pon_ports(NUM_OF_PON_PORTS);

    return Status::OK;
}

Status Enable_(int argc, char *argv[]) {
    bcmbal_access_terminal_cfg acc_term_obj;
    bcmbal_access_terminal_key key = { };

    if (!state.is_activated()) {
        std::cout << "Enable OLT" << std::endl;

        bcmbal_init(argc, argv, NULL);

        Status status = SubscribeIndication();
        if (!status.ok()) {
            std::cout << "ERROR: SubscribeIndication failed - "
                      << status.error_code() << ": " << status.error_message()
                      << std::endl;
            return status;
        }

        key.access_term_id = DEFAULT_ATERM_ID;
        BCMBAL_CFG_INIT(&acc_term_obj, access_terminal, key);
        BCMBAL_CFG_PROP_SET(&acc_term_obj, access_terminal, admin_state, BCMBAL_STATE_UP);
        bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(acc_term_obj.hdr));
        if (err) {
            std::cout << "ERROR: Failed to enable OLT" << std::endl;
            return bcm_to_grpc_err(err, "Failed to enable OLT");
        }
        init_stats();
    }

    //If already enabled, generate an extra indication ????
    return Status::OK;
}

Status Disable_() {
    // bcmbal_access_terminal_cfg acc_term_obj;
    // bcmbal_access_terminal_key key = { };
    //
    // if (state::is_activated) {
    //     std::cout << "Disable OLT" << std::endl;
    //     key.access_term_id = DEFAULT_ATERM_ID;
    //     BCMBAL_CFG_INIT(&acc_term_obj, access_terminal, key);
    //     BCMBAL_CFG_PROP_SET(&acc_term_obj, access_terminal, admin_state, BCMBAL_STATE_DOWN);
    //     bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(acc_term_obj.hdr));
    //     if (err) {
    //         std::cout << "ERROR: Failed to disable OLT" << std::endl;
    //         return bcm_to_grpc_err(err, "Failed to disable OLT");
    //     }
    // }
    // //If already disabled, generate an extra indication ????
    // return Status::OK;
    //This fails with Operation Not Supported, bug ???

    //TEMPORARY WORK AROUND
    Status status = DisableUplinkIf_(0);
    if (status.ok()) {
        state.deactivate();
        openolt::Indication ind;
        openolt::OltIndication* olt_ind = new openolt::OltIndication;
        olt_ind->set_oper_state("down");
        ind.set_allocated_olt_ind(olt_ind);
        std::cout << "Disable OLT, add an extra indication" << std::endl;
        oltIndQ.push(ind);
    }
    return status;

}

Status Reenable_() {
    Status status = EnableUplinkIf_(0);
    if (status.ok()) {
        state.activate();
        openolt::Indication ind;
        openolt::OltIndication* olt_ind = new openolt::OltIndication;
        olt_ind->set_oper_state("up");
        ind.set_allocated_olt_ind(olt_ind);
        std::cout << "Reenable OLT, add an extra indication" << std::endl;
        oltIndQ.push(ind);
    }
    return status;
}

Status EnablePonIf_(uint32_t intf_id) {
    bcmbal_interface_cfg interface_obj;
    bcmbal_interface_key interface_key;

    interface_key.intf_id = intf_id;
    interface_key.intf_type = BCMBAL_INTF_TYPE_PON;

    BCMBAL_CFG_INIT(&interface_obj, interface, interface_key);
    BCMBAL_CFG_PROP_SET(&interface_obj, interface, admin_state, BCMBAL_STATE_UP);

    bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(interface_obj.hdr));
    if (err) {
        std::cout << "ERROR: Failed to enable PON interface: " << intf_id << std::endl;
        return bcm_to_grpc_err(err, "Failed to enable PON interface");
    }

    return Status::OK;
}

Status DisableUplinkIf_(uint32_t intf_id) {
    bcmbal_interface_cfg interface_obj;
    bcmbal_interface_key interface_key;

    interface_key.intf_id = intf_id;
    interface_key.intf_type = BCMBAL_INTF_TYPE_NNI;

    BCMBAL_CFG_INIT(&interface_obj, interface, interface_key);
    BCMBAL_CFG_PROP_SET(&interface_obj, interface, admin_state, BCMBAL_STATE_DOWN);

    bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(interface_obj.hdr));
    if (err) {
        std::cout << "ERROR: Failed to disable Uplink interface: " << intf_id << std::endl;
        return bcm_to_grpc_err(err, "Failed to disable Uplink interface");
    }

    return Status::OK;
}

Status EnableUplinkIf_(uint32_t intf_id) {
    bcmbal_interface_cfg interface_obj;
    bcmbal_interface_key interface_key;

    interface_key.intf_id = intf_id;
    interface_key.intf_type = BCMBAL_INTF_TYPE_NNI;

    BCMBAL_CFG_INIT(&interface_obj, interface, interface_key);
    BCMBAL_CFG_PROP_SET(&interface_obj, interface, admin_state, BCMBAL_STATE_UP);

    bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(interface_obj.hdr));
    if (err) {
        std::cout << "ERROR: Failed to enable Uplink interface: " << intf_id << std::endl;
        return bcm_to_grpc_err(err, "Failed to enable Uplink interface");
    }

    return Status::OK;
}

Status DisablePonIf_(uint32_t intf_id) {
    bcmbal_interface_cfg interface_obj;
    bcmbal_interface_key interface_key;

    interface_key.intf_id = intf_id;
    interface_key.intf_type = BCMBAL_INTF_TYPE_PON;

    BCMBAL_CFG_INIT(&interface_obj, interface, interface_key);
    BCMBAL_CFG_PROP_SET(&interface_obj, interface, admin_state, BCMBAL_STATE_DOWN);

    bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(interface_obj.hdr));
    if (err) {
        std::cout << "ERROR: Failed to disable PON interface: " << intf_id << std::endl;
        return bcm_to_grpc_err(err, "Failed to disable PON interface");
    }

    return Status::OK;
}

Status ActivateOnu_(uint32_t intf_id, uint32_t onu_id,
    const char *vendor_id, const char *vendor_specific, uint32_t pir,
    uint32_t agg_port_id, uint32_t sched_id) {

    bcmbal_subscriber_terminal_cfg sub_term_obj = {};
    bcmbal_subscriber_terminal_key subs_terminal_key;
    bcmbal_serial_number serial_num = {};
    bcmbal_registration_id registration_id = {};

    std::cout << "Enabling ONU " << onu_id << " on PON " << intf_id << std::endl;
    std::cout << "Vendor Id " << vendor_id
              << "Vendor Specific Id " << vendor_specific
              << "pir " << pir
              << std::endl;

    subs_terminal_key.sub_term_id = onu_id;
    subs_terminal_key.intf_id = intf_id;
    BCMBAL_CFG_INIT(&sub_term_obj, subscriber_terminal, subs_terminal_key);

    memcpy(serial_num.vendor_id, vendor_id, 4);
    memcpy(serial_num.vendor_specific, vendor_specific, 4);
    BCMBAL_CFG_PROP_SET(&sub_term_obj, subscriber_terminal, serial_number, serial_num);

#if 0
    // Commenting out as this is causing issues with onu activation
    // with BAL 2.6 (Broadcom CS5248819).

    // FIXME - Use a default (all zeros) registration id.
    memset(registration_id.arr, 0, sizeof(registration_id.arr));
    BCMBAL_CFG_PROP_SET(&sub_term_obj, subscriber_terminal, registration_id, registration_id);
#endif

    BCMBAL_CFG_PROP_SET(&sub_term_obj, subscriber_terminal, admin_state, BCMBAL_STATE_UP);

    bcmos_errno err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(sub_term_obj.hdr));
    if (err) {
        std::cout << "ERROR: Failed to enable ONU: " << std::endl;
        return bcm_to_grpc_err(err, "Failed to enable ONU");
    }

    if (agg_port_id != 0) {
        return SchedAdd_(intf_id, onu_id, agg_port_id, sched_id, pir);
    } else {
        return SchedAdd_(intf_id, onu_id, mk_agg_port_id(intf_id, onu_id), sched_id, pir);
    }

    //return Status::OK;
}

Status DeactivateOnu_(uint32_t intf_id, uint32_t onu_id,
    const char *vendor_id, const char *vendor_specific) {

    bcmbal_subscriber_terminal_cfg sub_term_obj = {};
    bcmbal_subscriber_terminal_key subs_terminal_key;

    std::cout << "Deactivating ONU " << onu_id << " on PON " << intf_id << std::endl;
    std::cout << "Vendor Id " << vendor_id
              << "Vendor Specific Id " << vendor_specific
              << std::endl;

    subs_terminal_key.sub_term_id = onu_id;
    subs_terminal_key.intf_id = intf_id;
    BCMBAL_CFG_INIT(&sub_term_obj, subscriber_terminal, subs_terminal_key);

    BCMBAL_CFG_PROP_SET(&sub_term_obj, subscriber_terminal, admin_state, BCMBAL_STATE_DOWN);

    if (bcmbal_cfg_set(DEFAULT_ATERM_ID, &(sub_term_obj.hdr))) {
        std::cout << "ERROR: Failed to deactivate ONU: " << std::endl;
        return Status(grpc::StatusCode::INTERNAL, "Failed to deactivate ONU");
    }

    return Status::OK;
}

Status DeleteOnu_(uint32_t intf_id, uint32_t onu_id,
    const char *vendor_id, const char *vendor_specific,
    uint32_t agg_port_id, uint32_t sched_id) {

    // Need to deactivate before removing it (BAL rules)

    DeactivateOnu_(intf_id, onu_id, vendor_id, vendor_specific);
    // Sleep to allow the state to propagate
    // We need the subscriber terminal object to be admin down before removal
    // Without sleep the race condition is lost by ~ 20 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (agg_port_id != 0) {
        SchedRemove_(intf_id, onu_id, agg_port_id, sched_id);
    } else {
        SchedRemove_(intf_id, onu_id, mk_agg_port_id(intf_id, onu_id), sched_id);
    }

    bcmos_errno err = BCM_ERR_OK;
    bcmbal_subscriber_terminal_cfg cfg;
    bcmbal_subscriber_terminal_key key = { };

    std::cout << "Processing subscriber terminal cfg clear for sub_term_id = "
              << onu_id << " and intf_id = " << intf_id << std::endl;

    key.sub_term_id = onu_id ;
    key.intf_id = intf_id ;

    if (0 == key.sub_term_id)
    {
            std::cout << "Invalid Key to handle subscriber terminal clear subscriber_terminal_id = "
                      << onu_id << " Interface ID = " << intf_id << std::endl;
            return Status(grpc::StatusCode::INTERNAL, "Failed to delete ONU");
    }

    BCMBAL_CFG_INIT(&cfg, subscriber_terminal, key);

    err = bcmbal_cfg_clear(DEFAULT_ATERM_ID, &cfg.hdr);
    if (err != BCM_ERR_OK)
    {
       std::cout << "Failed to clear information for BAL subscriber_terminal_id = "
                << onu_id << " Interface ID = " << intf_id << std::endl;
        return Status(grpc::StatusCode::INTERNAL, "Failed to delete ONU");
    }

    return Status::OK;;
}

#define MAX_CHAR_LENGTH  20
#define MAX_OMCI_MSG_LENGTH 44
Status OmciMsgOut_(uint32_t intf_id, uint32_t onu_id, const std::string pkt) {
    bcmbal_u8_list_u32_max_2048 buf; /* A structure with a msg pointer and length value */
    bcmos_errno err = BCM_ERR_OK;

    /* The destination of the OMCI packet is a registered ONU on the OLT PON interface */
    bcmbal_dest proxy_pkt_dest;

    proxy_pkt_dest.type = BCMBAL_DEST_TYPE_ITU_OMCI_CHANNEL;
    proxy_pkt_dest.u.itu_omci_channel.sub_term_id = onu_id;
    proxy_pkt_dest.u.itu_omci_channel.intf_id = intf_id;

    // ???
    if ((pkt.size()/2) > MAX_OMCI_MSG_LENGTH) {
        buf.len = MAX_OMCI_MSG_LENGTH;
    } else {
        buf.len = pkt.size()/2;
    }

    /* Send the OMCI packet using the BAL remote proxy API */
    uint16_t idx1 = 0;
    uint16_t idx2 = 0;
    uint8_t arraySend[buf.len];
    char str1[MAX_CHAR_LENGTH];
    char str2[MAX_CHAR_LENGTH];
    memset(&arraySend, 0, buf.len);

    // std::cout << "Sending omci msg to ONU of length is "
    //      << buf.len
    //      << std::endl;

    for (idx1=0,idx2=0; idx1<((buf.len)*2); idx1++,idx2++) {
       sprintf(str1,"%c", pkt[idx1]);
       sprintf(str2,"%c", pkt[++idx1]);
       strcat(str1,str2);
       arraySend[idx2] = strtol(str1, NULL, 16);
    }

    buf.val = (uint8_t *)malloc((buf.len)*sizeof(uint8_t));
    memcpy(buf.val, (uint8_t *)arraySend, buf.len);
    //
    // std::cout << "After converting bytes to hex "
    //           << buf.val << buf.len << std::endl;

    err = bcmbal_pkt_send(0, proxy_pkt_dest, (const char *)(buf.val), buf.len);

    // std::cout << "OMCI request msg of length " << buf.len
    //           << " sent to ONU" << onu_id
    //           << " through PON " << intf_id << std::endl;

    free(buf.val);

    return Status::OK;
}

Status OnuPacketOut_(uint32_t intf_id, uint32_t onu_id, const std::string pkt) {
    bcmos_errno err = BCM_ERR_OK;
    bcmbal_dest proxy_pkt_dest;
    bcmbal_u8_list_u32_max_2048 buf;

    proxy_pkt_dest.type = BCMBAL_DEST_TYPE_SUB_TERM,
    proxy_pkt_dest.u.sub_term.sub_term_id = onu_id;
    proxy_pkt_dest.u.sub_term.intf_id = intf_id;

    buf.len = pkt.size();
    buf.val = (uint8_t *)malloc((buf.len)*sizeof(uint8_t));
    memcpy(buf.val, (uint8_t *)pkt.data(), buf.len);

    err = bcmbal_pkt_send(0, proxy_pkt_dest, (const char *)(buf.val), buf.len);

    std::cout << "Packet out of length " << buf.len
              << " sent to ONU" << onu_id
              << " through PON " << intf_id << std::endl;

    free(buf.val);

    return Status::OK;
}

Status UplinkPacketOut_(uint32_t intf_id, const std::string pkt) {
    bcmos_errno err = BCM_ERR_OK;
    bcmbal_dest proxy_pkt_dest;
    bcmbal_u8_list_u32_max_2048 buf;

    proxy_pkt_dest.type = BCMBAL_DEST_TYPE_NNI,
    proxy_pkt_dest.u.nni.intf_id = intf_id;

    buf.len = pkt.size();
    buf.val = (uint8_t *)malloc((buf.len)*sizeof(uint8_t));
    memcpy(buf.val, (uint8_t *)pkt.data(), buf.len);

    err = bcmbal_pkt_send(0, proxy_pkt_dest, (const char *)(buf.val), buf.len);

    std::cout << "Packet out of length " << buf.len
              << " sent through uplink port " << intf_id << std::endl;

    free(buf.val);

    return Status::OK;
}

Status FlowAdd_(uint32_t onu_id,
                uint32_t flow_id, const std::string flow_type,
                uint32_t access_intf_id, uint32_t network_intf_id,
                uint32_t gemport_id, uint32_t sched_id,
                uint32_t priority_value,
                const ::openolt::Classifier& classifier,
                const ::openolt::Action& action) {
    bcmos_errno err;
    bcmbal_flow_cfg cfg;
    bcmbal_flow_key key = { };

    std::cout << "flow add -"
              << " intf_id:" << access_intf_id
              << " onu_id:" << onu_id
              << " flow_id:" << flow_id
              << " flow_type:" << flow_type
              << " gemport_id:" << gemport_id
              << " network_intf_id:" << network_intf_id
              << std::endl;

    key.flow_id = flow_id;
    if (flow_type.compare("upstream") == 0 ) {
        key.flow_type = BCMBAL_FLOW_TYPE_UPSTREAM;
    } else if (flow_type.compare("downstream") == 0) {
        key.flow_type = BCMBAL_FLOW_TYPE_DOWNSTREAM;
    } else {
        std::cout << "Invalid flow type " << flow_type << std::endl;
        return bcm_to_grpc_err(BCM_ERR_PARM, "Invalid flow type");
    }

    BCMBAL_CFG_INIT(&cfg, flow, key);

    BCMBAL_CFG_PROP_SET(&cfg, flow, admin_state, BCMBAL_STATE_UP);
    BCMBAL_CFG_PROP_SET(&cfg, flow, access_int_id, access_intf_id);
    BCMBAL_CFG_PROP_SET(&cfg, flow, network_int_id, network_intf_id);
    BCMBAL_CFG_PROP_SET(&cfg, flow, sub_term_id, onu_id);
    BCMBAL_CFG_PROP_SET(&cfg, flow, svc_port_id, gemport_id);
    BCMBAL_CFG_PROP_SET(&cfg, flow, priority, priority_value);


    {
        bcmbal_classifier val = { };

        if (classifier.o_tpid()) {
            val.o_tpid = classifier.o_tpid();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_O_TPID;
        }

        if (classifier.o_vid()) {
            val.o_vid = classifier.o_vid();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_O_VID;
        }

        if (classifier.i_tpid()) {
            val.i_tpid = classifier.i_tpid();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_I_TPID;
        }

        if (classifier.i_vid()) {
            val.i_vid = classifier.i_vid();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_I_VID;
        }

        if (classifier.o_pbits()) {
            val.o_pbits = classifier.o_pbits();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_O_PBITS;
        }

        if (classifier.i_pbits()) {
            val.i_pbits = classifier.i_pbits();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_I_PBITS;
        }

        if (classifier.eth_type()) {
            val.ether_type = classifier.eth_type();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_ETHER_TYPE;
        }

        /*
        if (classifier.dst_mac()) {
            val.dst_mac = classifier.dst_mac();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_DST_MAC;
        }

        if (classifier.src_mac()) {
            val.src_mac = classifier.src_mac();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_SRC_MAC;
        }
        */

        if (classifier.ip_proto()) {
            val.ip_proto = classifier.ip_proto();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_IP_PROTO;
        }

        /*
        if (classifier.dst_ip()) {
            val.dst_ip = classifier.dst_ip();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_DST_IP;
        }

        if (classifier.src_ip()) {
            val.src_ip = classifier.src_ip();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_SRC_IP;
        }
        */

        if (classifier.src_port()) {
            val.src_port = classifier.src_port();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_SRC_PORT;
        }

        if (classifier.dst_port()) {
            val.dst_port = classifier.dst_port();
            val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_DST_PORT;
        }

        if (!classifier.pkt_tag_type().empty()) {
            if (classifier.pkt_tag_type().compare("untagged") == 0) {
                val.pkt_tag_type = BCMBAL_PKT_TAG_TYPE_UNTAGGED;
                val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_PKT_TAG_TYPE;
            } else if (classifier.pkt_tag_type().compare("single_tag") == 0) {
                val.pkt_tag_type = BCMBAL_PKT_TAG_TYPE_SINGLE_TAG;
                val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_PKT_TAG_TYPE;
            } else if (classifier.pkt_tag_type().compare("double_tag") == 0) {
                val.pkt_tag_type = BCMBAL_PKT_TAG_TYPE_DOUBLE_TAG;
                val.presence_mask = val.presence_mask | BCMBAL_CLASSIFIER_ID_PKT_TAG_TYPE;
            }
        }

        BCMBAL_CFG_PROP_SET(&cfg, flow, classifier, val);
    }

    {
        bcmbal_action val = { };

        const ::openolt::ActionCmd& cmd = action.cmd();

        if (cmd.add_outer_tag()) {
            val.cmds_bitmask |= BCMBAL_ACTION_CMD_ID_ADD_OUTER_TAG;
            val.presence_mask |= BCMBAL_ACTION_ID_CMDS_BITMASK;
        }

        if (cmd.remove_outer_tag()) {
            val.cmds_bitmask |= BCMBAL_ACTION_CMD_ID_REMOVE_OUTER_TAG;
            val.presence_mask |= BCMBAL_ACTION_ID_CMDS_BITMASK;
        }

        if (cmd.trap_to_host()) {
            val.cmds_bitmask |= BCMBAL_ACTION_CMD_ID_TRAP_TO_HOST;
            val.presence_mask |= BCMBAL_ACTION_ID_CMDS_BITMASK;
        }

        if (action.o_vid()) {
            val.o_vid = action.o_vid();
            val.presence_mask = val.presence_mask | BCMBAL_ACTION_ID_O_VID;
        }

        if (action.o_pbits()) {
            val.o_pbits = action.o_pbits();
            val.presence_mask = val.presence_mask | BCMBAL_ACTION_ID_O_PBITS;
        }

        if (action.o_tpid()) {
            val.o_tpid = action.o_tpid();
            val.presence_mask = val.presence_mask | BCMBAL_ACTION_ID_O_TPID;
        }

        if (action.i_vid()) {
            val.i_vid = action.i_vid();
            val.presence_mask = val.presence_mask | BCMBAL_ACTION_ID_I_VID;
        }

        if (action.i_pbits()) {
            val.i_pbits = action.i_pbits();
            val.presence_mask = val.presence_mask | BCMBAL_ACTION_ID_I_PBITS;
        }

        if (action.i_tpid()) {
            val.i_tpid = action.i_tpid();
            val.presence_mask = val.presence_mask | BCMBAL_ACTION_ID_I_TPID;
        }

        BCMBAL_CFG_PROP_SET(&cfg, flow, action, val);
    }

    {
        bcmbal_tm_sched_id val;
        if (sched_id != 0) {
            val = sched_id;
        } else {
        val = (bcmbal_tm_sched_id) mk_sched_id(access_intf_id, onu_id);
        }
        BCMBAL_CFG_PROP_SET(&cfg, flow, dba_tm_sched_id, val);
    }

    if (key.flow_type == BCMBAL_FLOW_TYPE_DOWNSTREAM) {
        bcmbal_tm_queue_ref val = { };
        val.sched_id = access_intf_id << 7 | onu_id;
        val.queue_id = 0;
        BCMBAL_CFG_PROP_SET(&cfg, flow, queue, val);
    }

    err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(cfg.hdr));
    if (err) {
        std::cout << "ERROR: flow add failed" << std::endl;
        return bcm_to_grpc_err(err, "flow add failed");
    }

    // register_new_flow(key);

    return Status::OK;
}

Status FlowRemove_(uint32_t flow_id, const std::string flow_type) {

    bcmbal_flow_cfg cfg;
    bcmbal_flow_key key = { };

    key.flow_id = (bcmbal_flow_id) flow_id;
    key.flow_id = flow_id;
    if (flow_type.compare("upstream") == 0 ) {
        key.flow_type = BCMBAL_FLOW_TYPE_UPSTREAM;
    } else if (flow_type.compare("downstream") == 0) {
        key.flow_type = BCMBAL_FLOW_TYPE_DOWNSTREAM;
    } else {
        std::cout << "Invalid flow type " << flow_type << std::endl;
        return bcm_to_grpc_err(BCM_ERR_PARM, "Invalid flow type");
    }

    BCMBAL_CFG_INIT(&cfg, flow, key);


    bcmos_errno err = bcmbal_cfg_clear(DEFAULT_ATERM_ID, &cfg.hdr);
    if (err) {
        std::cout << "Error " << err << " while removing flow "
            << flow_id << ", " << flow_type << std::endl;
        return Status(grpc::StatusCode::INTERNAL, "Failed to remove flow");
    }

    std::cout << "Flow " << flow_id << ", " << flow_type << " removed";
    return Status::OK;
}

Status SchedAdd_(int intf_id, int onu_id, int agg_port_id, int sched_id, int pir) {

    bcmos_errno err;

    /* Downstream */

    /* Create subscriber's tm_sched */
    {
        bcmbal_tm_sched_cfg cfg;
        bcmbal_tm_sched_key key = { };
        key.dir = BCMBAL_TM_SCHED_DIR_DS;
        key.id = intf_id << 7 | onu_id;
        BCMBAL_CFG_INIT(&cfg, tm_sched, key);

        bcmbal_tm_sched_owner owner = { };
        owner.type = BCMBAL_TM_SCHED_OWNER_TYPE_SUB_TERM;
        owner.u.sub_term.intf_id = intf_id;
        owner.u.sub_term.sub_term_id = onu_id;
        BCMBAL_CFG_PROP_SET(&cfg, tm_sched, owner, owner);

        bcmbal_tm_sched_parent parent = { };
        parent.sched_id = intf_id + 16384;
        parent.presence_mask = parent.presence_mask | BCMBAL_TM_SCHED_PARENT_ID_SCHED_ID;
        parent.weight = 1;
        parent.presence_mask = parent.presence_mask | BCMBAL_TM_SCHED_PARENT_ID_WEIGHT;
        BCMBAL_CFG_PROP_SET(&cfg, tm_sched, sched_parent, parent);

        BCMBAL_CFG_PROP_SET(&cfg, tm_sched, sched_type, BCMBAL_TM_SCHED_TYPE_WFQ);

        bcmbal_tm_shaping shaping = { };
        shaping.pir = pir;
        shaping.presence_mask = shaping.presence_mask | BCMBAL_TM_SHAPING_ID_PIR;
        BCMBAL_CFG_PROP_SET(&cfg, tm_sched, rate, shaping);

        err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &cfg.hdr);
        if (err) {
            std::cout << "ERROR: Failed to create subscriber downstream sched"
                      << " id:" << key.id
                      << " intf_id:" << intf_id
                      << " onu_id:" << onu_id << std::endl;
            return bcm_to_grpc_err(err, "Failed to create subscriber downstream sched");
        }
    }

    /* Create tm_queue */
    {
        bcmbal_tm_queue_cfg cfg;
        bcmbal_tm_queue_key key = { };
        key.sched_id = intf_id << 7 | onu_id;
        key.sched_dir = BCMBAL_TM_SCHED_DIR_DS;
        key.id = 0;

        BCMBAL_CFG_INIT(&cfg, tm_queue, key);
        BCMBAL_CFG_PROP_SET(&cfg, tm_queue, weight, 1);
        err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &cfg.hdr);

        if (err) {
            std::cout << "ERROR: Failed to create subscriber downstream tm queue"
                      << " id: " << key.id
                      << " sched_id: " << key.sched_id
                      << " intf_id: " << intf_id
                      << " onu_id: " << onu_id << std::endl;
            return bcm_to_grpc_err(err, "Failed to create subscriber downstream tm queue");
        }

    }

    /* Upstream */

    bcmbal_tm_sched_cfg cfg;
    bcmbal_tm_sched_key key = { };
    bcmbal_tm_sched_type sched_type;

    if (sched_id != 0) {
        key.id = sched_id;
    } else {
    key.id = mk_sched_id(intf_id, onu_id);
    }
    key.dir = BCMBAL_TM_SCHED_DIR_US;

    BCMBAL_CFG_INIT(&cfg, tm_sched, key);

    {
        bcmbal_tm_sched_owner val = { };

        val.type = BCMBAL_TM_SCHED_OWNER_TYPE_AGG_PORT;
        val.u.agg_port.intf_id = (bcmbal_intf_id) intf_id;
	    val.u.agg_port.presence_mask = val.u.agg_port.presence_mask | BCMBAL_TM_SCHED_OWNER_AGG_PORT_ID_INTF_ID;
        val.u.agg_port.sub_term_id = (bcmbal_sub_id) onu_id;
        val.u.agg_port.presence_mask = val.u.agg_port.presence_mask | BCMBAL_TM_SCHED_OWNER_AGG_PORT_ID_SUB_TERM_ID;
	    val.u.agg_port.agg_port_id = (bcmbal_aggregation_port_id) agg_port_id;
	    val.u.agg_port.presence_mask = val.u.agg_port.presence_mask | BCMBAL_TM_SCHED_OWNER_AGG_PORT_ID_AGG_PORT_ID;

        BCMBAL_CFG_PROP_SET(&cfg, tm_sched, owner, val);
    }

    err = bcmbal_cfg_set(DEFAULT_ATERM_ID, &(cfg.hdr));
    if (err) {
        std::cout << "ERROR: Failed to create upstream DBA sched"
                  << " id:" << key.id
                  << " intf_id:" << intf_id
                  << " onu_id:" << onu_id << std::endl;
        return bcm_to_grpc_err(err, "Failed to create upstream DBA sched");
    }
    std::cout << "create upstream DBA sched"
              << " id:" << key.id
              << " intf_id:" << intf_id
              << " onu_id:" << onu_id << std::endl;

    return Status::OK;
}

Status SchedRemove_(int intf_id, int onu_id, int agg_port_id, int sched_id) {

    bcmos_errno err;

    /* Upstream */

    bcmbal_tm_sched_cfg tm_cfg_us;
    bcmbal_tm_sched_key tm_key_us = { };

    if (sched_id != 0) {
        tm_key_us.id = sched_id;
    } else {
    tm_key_us.id = mk_sched_id(intf_id, onu_id);
    }
    tm_key_us.dir = BCMBAL_TM_SCHED_DIR_US;

    BCMBAL_CFG_INIT(&tm_cfg_us, tm_sched, tm_key_us);

    err = bcmbal_cfg_clear(DEFAULT_ATERM_ID, &(tm_cfg_us.hdr));
    if (err) {
        std::cout << "ERROR: Failed to remove upstream DBA sched"
                << " id:" << tm_key_us.id
                << " intf_id:" << intf_id
                << " onu_id:" << onu_id << std::endl;
        return Status(grpc::StatusCode::INTERNAL, "Failed to remove upstream DBA sched");
    }

    std::cout << "remove upstream DBA sched"
              << " id:" << tm_key_us.id
              << " intf_id:" << intf_id
              << " onu_id:" << onu_id << std::endl;

    /* Downstream */

    // Queue

    bcmbal_tm_queue_cfg queue_cfg;
    bcmbal_tm_queue_key queue_key = { };
    queue_key.sched_id = intf_id << 7 | onu_id;
    queue_key.sched_dir = BCMBAL_TM_SCHED_DIR_DS;
    queue_key.id = 0;

    BCMBAL_CFG_INIT(&queue_cfg, tm_queue, queue_key);

    err = bcmbal_cfg_clear(DEFAULT_ATERM_ID, &(queue_cfg.hdr));
    if (err) {
        std::cout << "ERROR: Failed to remove downstream tm queue"
                << " id:" << queue_key.id
                << " sched_id:" << queue_key.sched_id
                << " intf_id:" << intf_id
                << " onu_id:" << onu_id << std::endl;
        return Status(grpc::StatusCode::INTERNAL, "Failed to remove downstream tm queue");
    }

    std::cout << "remove upstream DBA sched"
              << " id:" << queue_key.id
              << " sched_id:" << queue_key.sched_id
              << " intf_id:" << intf_id
              << " onu_id:" << onu_id << std::endl;

    // Sheduler

    bcmbal_tm_sched_cfg tm_cfg_ds;
    bcmbal_tm_sched_key tm_key_ds = { };
    tm_key_ds.dir = BCMBAL_TM_SCHED_DIR_DS;
    tm_key_ds.id = intf_id << 7 | onu_id;
    BCMBAL_CFG_INIT(&tm_cfg_ds, tm_sched, tm_key_ds);

    err = bcmbal_cfg_clear(DEFAULT_ATERM_ID, &(tm_cfg_ds.hdr));
    if (err) {
        std::cout << "ERROR: Failed to remove sub downstream sched"
                << " id:" << tm_key_us.id
                << " intf_id:" << intf_id
                << " onu_id:" << onu_id << std::endl;
        return Status(grpc::StatusCode::INTERNAL, "Failed to remove sub downstream sched");
    }

    std::cout << "remove sub downstream sched"
              << " id:" << tm_key_us.id
              << " intf_id:" << intf_id
              << " onu_id:" << onu_id << std::endl;

    return Status::OK;
    //return 0;
}
