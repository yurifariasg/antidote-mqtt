/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/**
 * \file plugin_tcp_agent.c
 * \brief TCP plugin source.
 *
 * Copyright (C) 2011 Signove Tecnologia Corporation.
 * All rights reserved.
 * Contact: Signove Tecnologia Corporation (contact@signove.com)
 *
 * $LICENSE_TEXT:BEGIN$
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation and appearing
 * in the file LICENSE included in the packaging of this file; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 * $LICENSE_TEXT:END$
 *
 * IEEE 11073 Communication Model - Finite State Machine implementation
 *
 * \author Elvis Pfutzenreuter
 * \author Adrian Guedes
 * \author Fabricio Silva Epaminondas
 * \date Jun 28, 2011
 */

/**
 * @addtogroup AgentTcpPlugin
 * @{
 */

#include "src/util/strbuff.h"
#include "src/communication/communication.h"
#include "src/communication/plugin/plugin_mqtt_agent.h"
#include "src/util/log.h"
#include "src/util/ioutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdarg.h>
#include <stdarg.h>
#include <stdlib.h>
#include <mosquitto.h>

unsigned int plugin_id = 0;

static const int MQTT_ERROR = NETWORK_ERROR;
static const int MQTT_ERROR_NONE = NETWORK_ERROR_NONE;
static const int BACKLOG = 1;

// static int sk = -1;
static int port = 0;
// static intu8 *buffer = NULL;
// static int buffer_size = 0;
// static int buffer_retry = 0;

/**
 * Our Mosquitto Instance to connect with the mqtt broker
 */
static struct mosquitto *mosq = NULL;
/*
 * A Recent mosquitto message
 */
static struct mosquitto_message mosq_message;
static int messages = -1; // Invalidated

/*
 * Mosquitto Callbacks.
 */
void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
  DEBUG("[MESSAGE]\n");
  if(message->payloadlen){
    // DEBUG("%s %s\n", message->topic, (char*)message->payload);
    mosquitto_message_copy(&mosq_message, message);
    messages = 1;
  }else{
    DEBUG("%s (null)\n", message->topic);
  }
  // fflush(stdout);
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
  DEBUG("my_connect_callback\n");
  if(!result){
    /* Subscribe to broker information topics on successful connect. */
    // mosquitto_subscribe(mosq, NULL, "$SYS/#", 2);
    mosquitto_subscribe(mosq, NULL, "$hello/world/agent", 2);
  }else{
    ERROR("Connect failed\n");
  }
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
  int i;
  DEBUG("Subscribed (mid: %d): %d", mid, granted_qos[0]);
  for(i=1; i<qos_count; i++){
    DEBUG(", %d", granted_qos[i]);
  }
  DEBUG("\n");
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
  /* Pring all log messages regardless of level. */
  // DEBUG("[LOG] ");
  // DEBUG("%s\n", str);
}

/**
 * Initialize network layer.
 * Initialize network layer, in this case opens and initializes the mqtt connection.
 *
 * @return 1 if operation succeeds and 0 otherwise
 */
static int init_socket()
{
	DEBUG("network mqtt: starting socket");

	ContextId cid = {plugin_id, port};
	communication_transport_connect_indication(cid, "mqtt");

	return 1;
}

/**
 * Initialize network layer, in this case opens and initializes
 *  the file descriptors
 *
 * @return MQTT_ERROR_NONE if operation succeeds
 */
static int network_init(unsigned int plugin_label)
{
  DEBUG("network_init");
	plugin_id = plugin_label;

	if (init_socket()) {
		return MQTT_ERROR_NONE;
	}
  ERROR("error network_init");
	return MQTT_ERROR;
}

/**
 * Blocks to wait data to be available from the file descriptor
 *
 * @param ctx current connection context.
 * @return MQTT_ERROR_NONE if data is available or MQTT_ERROR if error.
 */
static int network_mqtt_wait_for_data(Context *ctx)
{
  DEBUG("network mqtt: network_wait_for_data");

  while (1) {
    if (messages != 0) {
      break;
    }
    sleep(1);
  }

  return MQTT_ERROR_NONE;

	// if (sk < 0) {
	// 	DEBUG("network tcp: network_wait_for_data error");
	// 	return MQTT_ERROR;
	// }

	// if (buffer_retry) {
		// there may be another APDU in buffer already
		// return MQTT_ERROR_NONE;
	// }

	// fd_set fds;

	// int ret_value;

	// while (1) {
	// 	if (sk < 0) {
	// 		return MQTT_ERROR;
	// 	}

	// 	FD_ZERO(&fds);
	// 	FD_SET(sk, &fds);

	// 	ret_value = select(sk + 1, &fds, NULL, NULL, NULL);
	// 	if (ret_value < 0) {
	// 		if (errno == EINTR) {
	// 			DEBUG(" network:fd Select failed with EINTR");
	// 			continue;
	// 		}
	// 		DEBUG(" network:fd Select failed");
	// 		return MQTT_ERROR;
	// 	} else if (ret_value == 0) {
	// 		DEBUG(" network:fd Select timeout");
	// 		return MQTT_ERROR;
	// 	}

	// 	break;
	// }
}

/**
 * Reads an APDU from the file descriptor
 * @param ctx
 * @return a byteStream with the read APDU or NULL if error.
 */
static ByteStreamReader *network_get_apdu_stream(Context *ctx)
{
	// ContextId cid = {plugin_id, port};

	// if (sk < 0) {
	// 	ERROR("network tcp: network_get_apdu_stream cannot found a valid sokcet");
	// 	communication_transport_disconnect_indication(cid, "mqtt");
	// 	return NULL;
	// }
  //
	// if (buffer_retry) {
	// 	// handling letover data in buffer
	// 	buffer_retry = 0;
	// } else {
	// 	intu8 localbuf[65535];
	// 	int bytes_read = read(sk, localbuf, 65535);
  //
	// 	if (bytes_read < 0) {
	// 		close(sk);
	// 		free(buffer);
	// 		buffer = 0;
	// 		buffer_size = 0;
	// 		communication_transport_disconnect_indication(cid, "mqtt");
	// 		DEBUG(" network:tcp error");
	// 		sk = -1;
	// 		return NULL;
	// 	} else if (bytes_read == 0) {
	// 		close(sk);
	// 		free(buffer);
	// 		buffer = 0;
	// 		buffer_size = 0;
	// 		communication_transport_disconnect_indication(cid, "mqtt");
	// 		DEBUG(" network:tcp closed");
	// 		sk = -1;
	// 		return NULL;
	// 	}
  //
	// 	buffer = realloc(buffer, buffer_size + bytes_read);
	// 	memcpy(buffer + buffer_size, localbuf, bytes_read);
	// 	buffer_size += bytes_read;
	// }
  //
	// if (buffer_size < 4) {
	// 	DEBUG(" network:tcp incomplete APDU (received %d)", buffer_size);
	// 	return NULL;
	// }
  //
	// int apdu_size = (buffer[2] << 8 | buffer[3]) + 4;
  //
	// if (buffer_size < apdu_size) {
	// 	DEBUG(" network:tcp incomplete APDU (expected %d received %d",
	// 	      					apdu_size, buffer_size);
	// 	return NULL;
	// }
  //

	// // Create bytestream
  DEBUG(" network:mqtt byte_stream_reder_instance... reading message...");
	ByteStreamReader *stream = byte_stream_reader_instance(mosq_message.payload, mosq_message.payloadlen);
  messages = 0;
	if (stream == NULL) {
		DEBUG(" network:tcp Error creating bytelib");
		return NULL;
	}
  //
	// buffer = 0;
	// buffer_size -= apdu_size;
	// if (buffer_size > 0) {
	// 	// leave next APDU in buffer
	// 	buffer_retry = 1;
	// 	buffer = malloc(buffer_size);
	// 	memcpy(buffer, stream->buffer_cur + apdu_size, buffer_size);
	// }
  //
	// DEBUG(" network:mqtt APDU received ");
	// ioutil_print_buffer(stream->buffer_cur, apdu_size);

	return stream;
  // DEBUG("Returning NULL");
  // return NULL;
}

/**
 * Sends an encoded apdu
 *
 * @param ctx
 * @param stream the apdu to be sent
 * @return MQTT_ERROR_NONE if data sent successfully and MQTT_ERROR otherwise
 */
static int network_send_apdu_stream(Context *ctx, ByteStreamWriter *stream)
{
  DEBUG("network_send_apdu_stream");
  sleep(1);

  mosquitto_publish(mosq, NULL, "$hello/world/manager",
      stream->size,
      stream->buffer,
      2, false);

	// unsigned int written = 0;

// 	while (written < stream->size) {
// 		int to_send = stream->size - written;
// #ifdef TEST_FRAGMENTATION
// 		to_send = to_send > 50 ? 50 : to_send;
// #endif
// 		int ret = write(sk, stream->buffer + written, to_send);
//
// 		DEBUG(" network:tcp sent %d bytes", to_send);
//
// 		if (ret <= 0) {
// 			DEBUG(" network:tcp Error sending APDU.");
// 			return MQTT_ERROR;
// 		}
//
// 		written += ret;
// 	}

	DEBUG(" network:mqtt APDU sent ");
	// ioutil_print_buffer(stream->buffer, stream->size);

	return MQTT_ERROR_NONE;
}

/**
 * Network disconnect
 *
 * @param ctx
 * @return MQTT_ERROR_NONE
 */
static int network_disconnect(Context *ctx)
{
	DEBUG("taking the initiative of disconnection");
  if (mosquitto_disconnect(mosq) == MOSQ_ERR_SUCCESS) {
    DEBUG("Disconnected... stopping loop");
    mosquitto_loop_stop(mosq, false);
  } else {
    DEBUG("Not disconnected... forcing...");
    mosquitto_loop_stop(mosq, true);
  }

	// close(sk);
	// sk = -1;
  //
	// free(buffer);
	// buffer = 0;
	// buffer_size = 0;
	// buffer_retry = 0;
  messages = -1;
  mosquitto_destroy(mosq);
  DEBUG("Terminated.");
  mosq = NULL;


  ContextId cid = {plugin_id, port};
  communication_transport_disconnect_indication(cid, "mqtt");

	return MQTT_ERROR_NONE;
}

/**
 * Finalizes network layer and deallocated data
 *
 * @return MQTT_ERROR_NONE if operation succeeds
 */
static int network_finalize()
{
  ERROR("mqtt_agent: network_finalize");
  mosquitto_lib_cleanup();

	return MQTT_ERROR_NONE;
}

static int create_socket()
{
  DEBUG("network mqtt: creating socket");

  mosq = mosquitto_new("test_agent", true, NULL);
  DEBUG("Created mosquitto instance");
  if (!mosq) {
    ERROR("Error: Out of memory.\n");
    return MQTT_ERROR;
  }

  mosquitto_log_callback_set(mosq, my_log_callback);
  mosquitto_connect_callback_set(mosq, my_connect_callback);
  mosquitto_message_callback_set(mosq, my_message_callback);
  mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);

  messages = 0;
  mosquitto_connect(mosq, "localhost", 1883, 300);
  DEBUG("Sent connection request");
  mosquitto_loop_start(mosq);
	return MQTT_ERROR_NONE;
}

/**
 * Initiate a CommunicationPlugin struct to use tcp connection.
 *
 * @param plugin CommunicationPlugin pointer
 * @param pport Port of the socket
 *
 * @return MQTT_ERROR if error
 */
int plugin_network_mqtt_agent_setup(CommunicationPlugin *plugin)
{
	DEBUG("network:mqtt Initializing agent socket");

	if (create_socket() == MQTT_ERROR) {
    ERROR("error plugin_network_mqtt_agent_setup");
		return MQTT_ERROR;
	}

	plugin->network_init = network_init;
	plugin->network_wait_for_data = network_mqtt_wait_for_data;
	plugin->network_get_apdu_stream = network_get_apdu_stream;
	plugin->network_send_apdu_stream = network_send_apdu_stream;
	plugin->network_disconnect = network_disconnect;
	plugin->network_finalize = network_finalize;

  DEBUG("network:mqtt Initializing agent socket finish");
	return MQTT_ERROR_NONE;
}

/** @} */
