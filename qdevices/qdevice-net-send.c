/*
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qdevice-net-log.h"
#include "qdevice-net-send.h"
#include "qdevice-net-votequorum.h"
#include "msg.h"

int
qdevice_net_send_echo_request(struct qdevice_net_instance *instance)
{
	struct send_buffer_list_entry *send_buffer;

	if (instance->echo_reply_received_msg_seq_num !=
	    instance->echo_request_expected_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Server didn't send echo reply message on time. "
		    "Disconnecting from server.");
		return (-1);
	}

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_CRIT, "Can't allocate send list buffer for reply msg.");

		return (-1);
	}

	instance->echo_request_expected_msg_seq_num++;

	if (msg_create_echo_request(&send_buffer->buffer, 1,
	    instance->echo_request_expected_msg_seq_num) == -1) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for echo request msg");

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

int
qdevice_net_send_init(struct qdevice_net_instance *instance)
{
	enum msg_type *supported_msgs;
	size_t no_supported_msgs;
	enum tlv_opt_type *supported_opts;
	size_t no_supported_opts;
	struct send_buffer_list_entry *send_buffer;

	tlv_get_supported_options(&supported_opts, &no_supported_opts);
	msg_get_supported_messages(&supported_msgs, &no_supported_msgs);
	instance->last_msg_seq_num++;

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for init msg");

		return (-1);
	}

	if (msg_create_init(&send_buffer->buffer, 1, instance->last_msg_seq_num,
	    instance->decision_algorithm,
	    supported_msgs, no_supported_msgs, supported_opts, no_supported_opts,
	    instance->node_id) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for init msg");

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_INIT_REPLY;

	return (0);
}

int
qdevice_net_send_ask_for_vote(struct qdevice_net_instance *instance)
{
	struct send_buffer_list_entry *send_buffer;

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for ask for vote msg");

		return (-1);
	}

	instance->last_msg_seq_num++;

	if (msg_create_ask_for_vote(&send_buffer->buffer, instance->last_msg_seq_num) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for ask for vote msg");

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

int
qdevice_net_send_config_node_list(struct qdevice_net_instance *instance, int initial)
{
	struct node_list nlist;
	struct send_buffer_list_entry *send_buffer;
	uint64_t config_version;
	int send_config_version;

	/*
	 * Send initial node list
	 */
	if (qdevice_net_cmap_get_nodelist(instance->cmap_handle, &nlist) != 0) {
		qdevice_net_log(LOG_ERR, "Can't get initial configuration node list.");

		return (-1);
	}

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for config "
		    "node list msg");

		node_list_free(&nlist);

		return (-1);
	}

	send_config_version = qdevice_net_cmap_get_config_version(instance->cmap_handle,
	    &config_version);

	instance->last_msg_seq_num++;

	if (msg_create_node_list(&send_buffer->buffer, instance->last_msg_seq_num,
	    (initial ? TLV_NODE_LIST_TYPE_INITIAL_CONFIG : TLV_NODE_LIST_TYPE_CHANGED_CONFIG),
	    0, NULL, send_config_version, config_version, 0, TLV_QUORATE_INQUORATE, &nlist) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for config list msg");

		node_list_free(&nlist);

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

int
qdevice_net_send_membership_node_list(struct qdevice_net_instance *instance,
    enum tlv_quorate quorate, const struct tlv_ring_id *ring_id,
    uint32_t node_list_entries, votequorum_node_t node_list[])
{
	struct node_list nlist;
	struct send_buffer_list_entry *send_buffer;
	uint32_t i;

	node_list_init(&nlist);

	for (i = 0; i < node_list_entries; i++) {
		if (node_list[i].nodeid == 0) {
			continue;
		}

		if (node_list_add(&nlist, node_list[i].nodeid, 0,
		    qdevice_net_votequorum_node_state_to_tlv(node_list[i].state)) == NULL) {
			qdevice_net_log(LOG_ERR, "Can't allocate membership node list.");

			node_list_free(&nlist);

			return (-1);
		}
	}

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for config "
		    "node list msg");

		node_list_free(&nlist);

		return (-1);
	}

	instance->last_msg_seq_num++;

	if (msg_create_node_list(&send_buffer->buffer, instance->last_msg_seq_num,
	    TLV_NODE_LIST_TYPE_MEMBERSHIP,
	    1, ring_id, 0, 0, 1, quorate, &nlist) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for config list msg");

		node_list_free(&nlist);

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}
