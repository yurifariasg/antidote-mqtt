/*
 * Author: Yuri Gomes
 */

#ifndef PLUGIN_MQTT_H_
#define PLUGIN_MQTT_H_

#include <communication/plugin/plugin.h>

int plugin_network_mqtt_setup(CommunicationPlugin *plugin, ...);
void plugin_network_mqtt_connect();


#endif /* PLUGIN_MQTT_H_ */
