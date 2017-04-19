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

#include <stdio.h>

#include "common/cs_dbg.h"
#include "common/json_utils.h"
#include "common/platform.h"
#include "frozen/frozen.h"
#include "fw/src/mgos_app.h"
#include "fw/src/mgos_gpio.h"
#include "fw/src/mgos_mqtt.h"
#include "fw/src/mgos_rpc.h"
#include "fw/src/mgos_wifi.h"

static void* TURN_ON = "n";
static void* TURN_OFF = "f";
static void* GET_STATE = "s";
static void* ENABLE = "e";
static void* DISABLE = "d";

static bool sonoff_led_disabled = false;
static bool sonoff_button_disabled = false;

static void led_on(bool on) {
	if (sonoff_led_disabled) {
		// high is off.
		mgos_gpio_write(13, true);
		return;
	}

	if (on) {
		// low is on.
		mgos_gpio_write(13, false);
	} else {
		// high is off.
		mgos_gpio_write(13, true);
	}
}

static void sonoff_relay_handler(struct mg_rpc_request_info *ri, void *cb_arg,
                                 struct mg_rpc_frame_info *fi, struct mg_str args) {
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	mbuf_init(&fb, 128);
	struct sys_config *cfg = get_cfg();

	int err = 0;
	if (cb_arg == TURN_ON) {
		led_on(true);

		// turn the relay on by setting it high
		mgos_gpio_write(12, true);
	} else if (cb_arg == TURN_OFF) {
		led_on(false);

		// turn the relay on by setting it low
		mgos_gpio_write(12, false);
	} else {
		err = 500;
		json_printf(&out, "no or invalid action provided to callback");
	}

	if (err == 0) {
		json_printf(&out, "{device_id: %Q, device_type: sonoff, relay_on: %d}", cfg->device.id, mgos_gpio_read(12) ? 1 : 0);
		mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
		if (cb_arg != GET_STATE && strlen(cfg->sonoff.mqtt_topic) > 0)
			mgos_mqtt_pub(cfg->sonoff.mqtt_topic, fb.buf, fb.len);
	} else {
		mg_rpc_send_errorf(ri, err, "%.*s", fb.len, fb.buf);
	}
	
	ri = NULL;
	mbuf_free(&fb);
	(void) args;
	(void) cb_arg;
	(void) fi;
}

static void sonoff_button_handler(struct mg_rpc_request_info *ri, void *cb_arg,
                                  struct mg_rpc_frame_info *fi, struct mg_str args) {
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	mbuf_init(&fb, 128);
	struct sys_config *cfg = get_cfg();

	int err = 0;
	if (cb_arg == ENABLE) {
		sonoff_button_disabled = false;
	} else if (cb_arg == DISABLE) {
		sonoff_button_disabled = true;
	} else {
		err = 500;
		json_printf(&out, "no or invalid action provided to callback");
	}

	if (err == 0) {
		json_printf(&out, "{device_id: %Q, device_type: sonoff, button_disabled: %d}", cfg->device.id, sonoff_button_disabled ? 1 : 0);
		mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
		if (cb_arg != GET_STATE && strlen(cfg->sonoff.mqtt_topic) > 0)
			mgos_mqtt_pub(cfg->sonoff.mqtt_topic, fb.buf, fb.len);
	} else {
		mg_rpc_send_errorf(ri, err, "%.*s", fb.len, fb.buf);
	}
	
	ri = NULL;
	mbuf_free(&fb);
	(void) args;
	(void) cb_arg;
	(void) fi;
}

static void sonoff_led_handler(struct mg_rpc_request_info *ri, void *cb_arg,
                               struct mg_rpc_frame_info *fi, struct mg_str args) {
	struct mbuf fb;
	struct json_out out = JSON_OUT_MBUF(&fb);
	mbuf_init(&fb, 128);
	struct sys_config *cfg = get_cfg();

	int err = 0;
	if (cb_arg == ENABLE) {
		sonoff_led_disabled = false;
		led_on(mgos_gpio_read(12) ? 1 : 0);
	} else if (cb_arg == DISABLE) {
		sonoff_led_disabled = true;
		led_on(false);
	} else {
		err = 500;
		json_printf(&out, "no or invalid action provided to callback");
	}

	if (err == 0) {
		json_printf(&out, "{device_id: %Q, device_type: sonoff, led_disabled: %d}", cfg->device.id, sonoff_led_disabled ? 1 : 0);
		mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
		if (cb_arg != GET_STATE && strlen(cfg->sonoff.mqtt_topic) > 0)
			mgos_mqtt_pub(cfg->sonoff.mqtt_topic, fb.buf, fb.len);
	} else {
		mg_rpc_send_errorf(ri, err, "%.*s", fb.len, fb.buf);
	}
	
	ri = NULL;
	mbuf_free(&fb);
	(void) args;
	(void) cb_arg;
	(void) fi;
}

static void sonoff_toggle() {
	// Skip if the button is disabled.
	if (sonoff_button_disabled)
		return;

	// toggle the LED and relay
	mgos_gpio_toggle(13);
	mgos_gpio_toggle(12);

	struct sys_config *cfg = get_cfg();
	if (strlen(cfg->sonoff.mqtt_topic) > 0) {
		struct mbuf fb;
		struct json_out out = JSON_OUT_MBUF(&fb);
		mbuf_init(&fb, 128);
		json_printf(&out, "{device_id: %Q, device_type: sonoff, relay_on: %d}", cfg->device.id, mgos_gpio_read(12) ? 1 : 0);
		mgos_mqtt_pub(cfg->sonoff.mqtt_topic, fb.buf, fb.len);
	}

}

enum mgos_app_init_result mgos_app_init(void) {
	// Set LED and Relay pins to outputs and off
	mgos_gpio_set_mode(13, MGOS_GPIO_MODE_OUTPUT);
	mgos_gpio_write(13, true);
	mgos_gpio_set_mode(12, MGOS_GPIO_MODE_OUTPUT);
	mgos_gpio_write(12, false);

	// register the sonoff RPC handlers
	struct mg_rpc *c = mgos_rpc_get_global();
	mg_rpc_add_handler(c, "Sonoff.Relay.On", "{}", sonoff_relay_handler, TURN_ON);
	mg_rpc_add_handler(c, "Sonoff.Relay.Off", "{}", sonoff_relay_handler, TURN_OFF);
	mg_rpc_add_handler(c, "Sonoff.Relay.Status", "{}", sonoff_relay_handler, GET_STATE);
	
	mg_rpc_add_handler(c, "Sonoff.Button.Enable", "{}", sonoff_button_handler, ENABLE);
	mg_rpc_add_handler(c, "Sonoff.Button.Disable", "{}", sonoff_button_handler, DISABLE);
	mg_rpc_add_handler(c, "Sonoff.Button.Status", "{}", sonoff_button_handler, GET_STATE);
	
	mg_rpc_add_handler(c, "Sonoff.LED.Enable", "{}", sonoff_led_handler, ENABLE);
	mg_rpc_add_handler(c, "Sonoff.LED.Disable", "{}", sonoff_led_handler, DISABLE);
	mg_rpc_add_handler(c, "Sonoff.LED.Status", "{}", sonoff_led_handler, GET_STATE);

	// register a button handler to toggle state
	mgos_gpio_set_button_handler(0, MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_ANY, 500, sonoff_toggle, NULL);

	return MGOS_APP_INIT_SUCCESS;
}

