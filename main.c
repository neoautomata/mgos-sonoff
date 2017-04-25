/*
   Copyright 2017 neoautomata

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "common/cs_dbg.h"
#include "common/json_utils.h"
#include "common/platform.h"
#include "frozen/frozen.h"
#include "fw/src/mgos_adc.h"
#include "fw/src/mgos_app.h"
#include "fw/src/mgos_console.h"
#include "fw/src/mgos_gpio.h"
#include "fw/src/mgos_mqtt.h"
#include "fw/src/mgos_timers.h"
#include "fw/src/mgos_rpc.h"
#include "fw/src/mgos_wifi.h"

#include "lib/DHT/dht.h"
dht_data *dht1;

static void* NO_MOTION = 0;
static void* MOTION_DETECTED = 1;

static void dht_handler(struct mg_rpc_request_info *ri, void *cb_arg,
                        struct mg_rpc_frame_info *fi, struct mg_str args) {
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	struct sys_config *cfg = get_cfg();
	mbuf_init(&fb, 192);

	if(dht1->err != NULL) {
		json_printf(&out, "{device_id: %Q, device_type: sensors, msg: %Q}", cfg->device.id, "last dht read failed");
		mg_rpc_send_errorf(ri, 500, "%.*s", fb.len, fb.buf);
	} else {
		json_printf(&out, "{device_id: %Q, device_type: sensors, temp: %.2f, humidity: %.2f}", cfg->device.id, dht1->temp, dht1->humidity);
		mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
	}

	ri = NULL;
	mbuf_free(&fb);

	(void) args;
	(void) cb_arg;
	(void) fi;
}

static void dht_read() {
	struct sys_config *cfg = get_cfg();
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	mbuf_init(&fb, 192);

	dht_data *tmp = Read_DHT(cfg->dht1.pin, cfg->dht1.farenheit);
	free(dht1);
	dht1 = tmp;
	if(dht1->err != NULL) {
		CONSOLE_LOG(LL_INFO, ("Read dht - err: %s", dht1->err));
		json_printf(&out, "{device_id: %Q, device_type: sensors, sensor: dht1, err: %s}", cfg->device.id, dht1->err);
	} else {
		CONSOLE_LOG(LL_INFO, ("Read dht - temp: %.2f; humidity: %.2f", dht1->temp, dht1->humidity));
		json_printf(&out, "{device_id: %Q, device_type: sensors, sensor: dht1, temp: %.2f, humidity: %.2f}", cfg->device.id, dht1->temp, dht1->humidity);
	}
	if (strlen(cfg->dht1.mqtt_topic) > 0) {
		mgos_mqtt_pub(cfg->dht1.mqtt_topic, fb.buf, fb.len);
	}

	mbuf_free(&fb);
}

static void light_handler(struct mg_rpc_request_info *ri, void *cb_arg,
                          struct mg_rpc_frame_info *fi, struct mg_str args) {
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	struct sys_config *cfg = get_cfg();
	mbuf_init(&fb, 192);

	json_printf(&out, "{device_id: %Q, device_type: sensors, sensor: light1, level: %d}", cfg->device.id, mgos_adc_read(0));
	mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);

	ri = NULL;
	mbuf_free(&fb);

	(void) args;
	(void) cb_arg;
	(void) fi;
}

static void light_read() {
	struct sys_config *cfg = get_cfg();
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	mbuf_init(&fb, 192);

	int lvl = mgos_adc_read(0);
	CONSOLE_LOG(LL_INFO, ("Read light - lvl: %d", lvl));
	json_printf(&out, "{device_id: %Q, device_type: sensors, sensor: light1, level: %d}", cfg->device.id, lvl);
	if (strlen(cfg->light1.mqtt_topic) > 0)
		mgos_mqtt_pub(cfg->light1.mqtt_topic, fb.buf, fb.len);

	mbuf_free(&fb);
}

static void motion_interrupt(int pin, void *arg) {
	struct sys_config *cfg = get_cfg();

	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	mbuf_init(&fb, 192);
	CONSOLE_LOG(LL_INFO, ("Motion interrupt - motion detected: %d", arg == MOTION_DETECTED ? 1 : 0));
	json_printf(&out, "{device_id: %Q, device_type: sensors, sensor: motion1, motion_detected: %d}", cfg->device.id, arg == MOTION_DETECTED ? 1 : 0);
	if (strlen(cfg->light1.mqtt_topic) > 0)
		mgos_mqtt_pub(cfg->light1.mqtt_topic, fb.buf, fb.len);

	mbuf_free(&fb);
	(void) pin;
}

enum mgos_app_init_result mgos_app_init(void) {
	struct sys_config *cfg = get_cfg();
	struct mg_rpc *c = mgos_rpc_get_global();
 
	if (cfg->dht1.enable) {
		CONSOLE_LOG(LL_INFO, ("DHT1 enabled: pin=%d, farenheit=%d, period=%dms, mqtt_topic=%s", cfg->dht1.pin, cfg->dht1.farenheit, cfg->dht1.period, cfg->dht1.mqtt_topic));

		mgos_set_timer(cfg->dht1.period, 1, dht_read, NULL);

		// Register the read handler.
		mg_rpc_add_handler(c, "DHT1.Read", "{}", dht_handler, NULL);
	}

	if (cfg->light1.enable) {
		CONSOLE_LOG(LL_INFO, ("Light1 enabled: period=%dms, mqtt_topic=%s", cfg->light1.period, cfg->light1.mqtt_topic));

		mgos_set_timer(cfg->light1.period, 1, light_read, NULL);

		// Register the read handler.
		mg_rpc_add_handler(c, "Light1.Read", "{}", light_handler, NULL);
	}
	
	if (cfg->motion1.enable) {
		CONSOLE_LOG(LL_INFO, ("Motion1 enabled: pin=%d, mqtt_topic=%s", cfg->motion1.pin, cfg->motion1.mqtt_topic));

		// register handlers that detect rising (motion detected) and
		// falling (still) edges.
		mgos_gpio_set_int_handler(cfg->motion1.pin, MGOS_GPIO_INT_EDGE_POS, motion_interrupt, MOTION_DETECTED);
		mgos_gpio_set_int_handler(cfg->motion1.pin, MGOS_GPIO_INT_EDGE_NEG, motion_interrupt, NO_MOTION);
		mgos_gpio_enable_int(cfg->motion1.pin);
	}

	mgos_wifi_add_on_change_cb(on_wifi_event, 0);
	return MGOS_APP_INIT_SUCCESS;
}
