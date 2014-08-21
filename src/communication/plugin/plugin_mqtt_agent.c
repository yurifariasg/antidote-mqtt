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

#define MQTT_QOS 1

static const int MQTT_ERROR = NETWORK_ERROR;
static const int MQTT_ERROR_NONE = NETWORK_ERROR_NONE;
static const int BACKLOG = 1;

static int port = 0;

/**
 * Our Mosquitto Instance to connect with the mqtt broker
 */
static struct mosquitto *mosq = NULL;

#define push(sp, n) (*((sp)++) = (n))
#define pop(sp) (*--(sp))

/*
 * A Recent mosquitto message
 */
//static struct mosquitto_message mosq_message_aux;
static int messages = -1; // -1 = Invalidated
static int subscribed = 0;

static const struct mosquitto_message* stack[1000];
static const struct mosquitto_message* *sp;

/*
 * Mosquitto Callbacks.
 */
void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
  DEBUG("[MQTT] Message Received");
  if(message->payloadlen){
    //mosquitto_message *received_message = new mosquitto_message();
    //mosquitto_message_copy(&mosq_message, message);
    struct mosquitto_message* received_message = (struct mosquitto_message*)malloc(sizeof(struct mosquitto_message) );// = {};
    mosquitto_message_copy(received_message, message);

    push(sp, received_message);
    messages = messages + 1;
    DEBUG("[MQTT] Message Count: %d", messages);
  }
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
  if(!result){
    /* Subscribe to broker information topics on successful connect. */
    DEBUG("[MQTT] Connected! Sending subscribe request...\n");
    mosquitto_subscribe(mosq, NULL, "$agent", MQTT_QOS);
  }else{
    ERROR("[MQTT] Connect failed\n");
  }
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
  DEBUG("[MQTT] Subscribed (mid: %d)", mid);
  subscribed = 1;
  messages = 0;
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

  sp = stack;

  #ifdef MQTT_LOGS
  mosquitto_log_callback_set(mosq, my_log_callback);
  #endif
  mosquitto_connect_callback_set(mosq, my_connect_callback);
  mosquitto_message_callback_set(mosq, my_message_callback);
  mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);
  
  subscribed = 0;
  mosquitto_connect(mosq, "localhost", 1883, 300);
  mosquitto_loop_start(mosq);

  // Wait until connects..
  while (subscribed == 0) {
    sleep(0.0001);
  }
  ContextId cid = {plugin_id, port};
  communication_transport_connect_indication(cid, "mqtt");

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

  if (messages > 0) {
    messages = messages - 1;
    const struct mosquitto_message* message = pop(sp);
    ByteStreamReader *stream = byte_stream_reader_instance(message->payload, message->payloadlen);

    if (stream == NULL) {
      DEBUG("[MQTT] network:Error creating bytelib");
      return NULL;
    }
    return stream;
  } else {
    return NULL;
  }
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
  while (subscribed == 0) {
    DEBUG("Waiting on send apdu...");
    sleep(0.0001);
  }

  DEBUG("[MQTT] network_send_apdu_stream");

  while (mosquitto_publish(mosq, NULL, "$manager",
      stream->size,
      stream->buffer,
      MQTT_QOS, false) != MOSQ_ERR_SUCCESS) {
    DEBUG("Failed to Publish.. Retrying..");
    sleep(1);
  }


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
 * Initiate a CommunicationPlugin struct to use mqtt connection.
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
