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

unsigned int plugin_id = 1;

// #define MQTT_LOGS 1

static const int MQTT_ERROR = NETWORK_ERROR;
static const int MQTT_ERROR_NONE = NETWORK_ERROR_NONE;
static const int BACKLOG = 1;

static int port = 0;

/**
 * Our Mosquitto Instance to connect with the mqtt broker
 */
static struct mosquitto *mosq = NULL;
/*
 * A Recent mosquitto message
 */
static struct mosquitto_message mosq_message;
static int messages = -1; // -1 = Invalidated

/*
 * Mosquitto Callbacks.
 */
void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
  DEBUG("[MQTT] Message Received");
  if(message->payloadlen){
    mosquitto_message_copy(&mosq_message, message);
    messages = 1;
  }
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
  if(!result){
    /* Subscribe to broker information topics on successful connect. */
    DEBUG("[MQTT] Connected! Sending subscribe request...\n");
    mosquitto_subscribe(mosq, NULL, "$agent", 2);
  }else{
    ERROR("[MQTT] Connect failed\n");
  }
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
  DEBUG("[MQTT] Subscribed (mid: %d)", mid);
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
  /* Pring all log messages regardless of level. */
  DEBUG("[MQTT][LOG] %s", str);
}

/**
 * Initialize network layer, in this case opens and initializes
 *  the file descriptors
 *
 * @return MQTT_ERROR_NONE if operation succeeds
 */
static int network_init(unsigned int plugin_label)
{
  DEBUG("[MQTT] network_init with plugin_label: %d", plugin_label);

  mosq = mosquitto_new("test_agent", true, NULL);
  if (!mosq) {
    ERROR("[MQTT] Error: Out of memory.\n");
    return MQTT_ERROR;
  }

  #ifdef MQTT_LOGS
  mosquitto_log_callback_set(mosq, my_log_callback);
  #endif
  mosquitto_connect_callback_set(mosq, my_connect_callback);
  mosquitto_message_callback_set(mosq, my_message_callback);
  mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);

  mosquitto_connect(mosq, "localhost", 1883, 300);
  mosquitto_loop_start(mosq);

  // Wait until connects..
  sleep(1);
  ContextId cid = {plugin_id, port};
  communication_transport_connect_indication(cid, "tcp");

  return MQTT_ERROR_NONE;
}

/**
 * Blocks to wait data to be available from the file descriptor
 *
 * @param ctx current connection context.
 * @return MQTT_ERROR_NONE if data is available or MQTT_ERROR if error.
 */
static int network_mqtt_wait_for_data(Context *ctx)
{
  DEBUG("[MQTT] network_mqtt_wait_for_data");

  while (1) {
    if (messages != 0) {
      break;
    }
    sleep(0.1);
  }

  return MQTT_ERROR_NONE;
}

/**
 * Reads an APDU from the file descriptor
 * @param ctx
 * @return a byteStream with the read APDU or NULL if error.
 */
static ByteStreamReader *network_get_apdu_stream(Context *ctx)
{
  DEBUG("[MQTT] network_get_apdu_stream. Currently with %d messages", messages);
  messages = 0;
  ByteStreamReader *stream = byte_stream_reader_instance(mosq_message.payload, mosq_message.payloadlen);

  if (stream == NULL) {
    DEBUG("[MQTT] network:Error creating bytelib");
    return NULL;
  }
  return stream;
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
  DEBUG("[MQTT] network_send_apdu_stream");

  mosquitto_publish(mosq, NULL, "$manager",
      stream->size,
      stream->buffer,
      2, false);

  DEBUG("[MQTT] network: APDU sent ");

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
  DEBUG("[MQTT] network: lib init");

  mosquitto_lib_init();
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
  DEBUG("[MQTT] network: Initializing sockets");

  if (create_socket() == MQTT_ERROR) {
    return MQTT_ERROR;
  }

	plugin->network_init = network_init;
	plugin->network_wait_for_data = network_mqtt_wait_for_data;
	plugin->network_get_apdu_stream = network_get_apdu_stream;
	plugin->network_send_apdu_stream = network_send_apdu_stream;
	plugin->network_disconnect = network_disconnect;
	plugin->network_finalize = network_finalize;

	return MQTT_ERROR_NONE;
}

/** @} */
