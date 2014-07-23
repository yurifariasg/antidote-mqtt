#include <stdio.h>
#include <mosquitto.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
	printf("Message Received\n");
	if(message->payloadlen){
		printf("%s %s\n", message->topic, message->payload);
	}else{
		printf("%s (null)\n", message->topic);
	}
	fflush(stdout);
}

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
	printf("Publish Successful\n");
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
	if(!result){
		printf("Connected\n");
		/* Publish to broker on successful connect. */
		char* payload = "Hello World";
		int payloadlen = strlen(payload);
                mosquitto_subscribe(mosq, NULL, "$hello/world", 2);
                sleep(5);
		mosquitto_publish(mosq, NULL, "$hello/world", payloadlen, payload, 2, false);
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
	int i;
	printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++){
		printf(", %d", granted_qos[i]);
	}
	printf("\n");
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
	/* Pring all log messages regardless of level. */
	printf("[LOG] ");
	printf("%s\n", str);
}

static int run = 0;

void sig_handler(int signo)
{
	if (signo == SIGINT) {
		printf("received SIGINT\n");
		run = 1;
	}
}

int main(int argc, char *argv[])
{
	signal(SIGINT, sig_handler);
	char* id = "test_agent";
	char *host = "localhost";
	int port = 1883;
	int keepalive = 60;
	bool clean_session = true;
	struct mosquitto *mosq = NULL;

	mosquitto_lib_init();
	mosq = mosquitto_new(id, clean_session, NULL);
	printf("Created instance with id: %s\n", id);
	if(!mosq){
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	mosquitto_log_callback_set(mosq, my_log_callback);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);
	mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);
	mosquitto_publish_callback_set(mosq, my_publish_callback);

	if(mosquitto_connect(mosq, host, port, keepalive) != MOSQ_ERR_SUCCESS){
		fprintf(stderr, "Unable to connect.\n");
		return 1;
	}

	int lresult = MOSQ_ERR_SUCCESS;
	while((lresult = mosquitto_loop(mosq, 1000, 10)) == MOSQ_ERR_SUCCESS && run == 0){
		// Every Second...
		sleep(1);
	}

	if (lresult == MOSQ_ERR_INVAL) {
		printf("The input parameters were invalid.\n");
	} else if (lresult == MOSQ_ERR_NOMEM) {
		printf("An out of memory condition occurred.");
	} else if (lresult == MOSQ_ERR_NO_CONN) {
		printf("The client isn’t connected to a broker.\n");
	} else if (lresult == MOSQ_ERR_CONN_LOST) {
		printf("The connection to the broker was lost.\n");
	} else if (lresult == MOSQ_ERR_PROTOCOL) {
		printf("There is a protocol error communicating with the broker.\n");
	} else {
		printf("Unknown\n");
	}

	printf("Disconnecting...\n");
	mosquitto_disconnect(mosq);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return 0;
}
