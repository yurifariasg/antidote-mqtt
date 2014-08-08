#include "src/util/strbuff.h"
#include "src/communication/communication.h"
#include "src/communication/plugin/plugin_mqtt.h"
#include "src/util/log.h"
#include "src/util/ioutil.h"
#include "src/util/linkedlist.h"
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

// #define TEST_FRAGMENTATION 1
// #define MQTT_LOGS 1

/**
 * Plugin ID attributed by stack
 */
static unsigned int plugin_id = 1;

/**
 * \cond Undocumented
 */
static const int MQTT_ERROR = NETWORK_ERROR;
static const int MQTT_ERROR_NONE = NETWORK_ERROR_NONE;
static const int BACKLOG = 1;

/**
 * Our Mosquitto Instance to connect with the mqtt broker
 */
static struct mosquitto *mosq;
/*
 * A Recent mosquitto message
 */
static struct mosquitto_message mosq_message;
static int messages = 0;

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
    mosquitto_subscribe(mosq, NULL, "$manager", 2);
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
 * Notify stack that there is a connection (and it should create a context)
 */
void plugin_network_mqtt_connect()
{
	ContextId cid = {plugin_id, 1};
	communication_transport_connect_indication(cid, "mqtt");
}

/**
 * Initialize network layer, in this case opens and initializes
 *  the file descriptors
 *
 * @param plugin_label the Plugin ID or label attributed by stack to this plugin
 * @return MQTT_ERROR_NONE if operation succeeds
 */
static int network_init(unsigned int plugin_label)
{
  DEBUG("[MQTT] network_init with plugin_label: %d", plugin_label)

  mosq = mosquitto_new("test_manager", true, NULL);
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
    sleep(0.0001);
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
 * @param ctx Context
 * @param stream the apdu to be sent
 * @return MQTT_ERROR_NONE if data sent successfully and MQTT_ERROR otherwise
 */
static int network_send_apdu_stream(Context *ctx, ByteStreamWriter *stream)
{
  DEBUG("[MQTT] network_send_apdu_stream");

  mosquitto_publish(mosq, NULL, "$agent",
      stream->size,
      stream->buffer,
      0, false);

	DEBUG("[MQTT] network: APDU sent ");

	return MQTT_ERROR_NONE;
}

/**
 * Finalizes a socket (can be re-initialized again)
 *
 * @param element contains a NetworkSocket struct pointer
 */
// static int fin_socket(void *element)
// {
// 	return 1;
//
// }

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

  ContextId cid = {plugin_id, 1};
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

/**
 * Creates a listener socket and NetworkSocket struct for the given port
 *
 * @return MQTT_ERROR_NONE if ok
 */
static int create_socket()
{
	DEBUG("[MQTT] network: lib init");

  mosquitto_lib_init();
	return MQTT_ERROR_NONE;
}

/**
 * Initiate a CommunicationPlugin struct to use mqtt connection.
 *
 * @param plugin CommunicationPlugin pointer
 * @param numberOfPorts number of socket ports
 *
 * @return MQTT_ERROR if error
 */
int plugin_network_mqtt_setup(CommunicationPlugin *plugin, ...)
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
