#include "ble_node_control.h"


static GFont s_res_gothic_18;
static GFont s_res_gothic_14;
static GFont s_res_bitham_30_black;




typedef struct {
  BTDevice device;
  Window *window;

 TextLayer *s_textlayer_temp;
 TextLayer *s_textlayer_humidity;
 TextLayer *s_textlayer_pressure;
 TextLayer *s_textlayer_status;


//   TextLayer *s_textlayer_status;
  char info_text_buffer[64];
	char temp_text_buffer[64];
	char pressure_text_buffer[64];
	char humidity_text_buffer[64];
  char node_command_buffer[32];
  char read_node_buffer[32];
  uint16_t read_node_buffer_pos;
  uint16_t read_node_buffer_max;

  BLECharacteristic node_service_1_characteristic;
  BLECharacteristic node_command_characteristic;
  BLECharacteristic node_service_4_characteristic;
  BLECharacteristic node_service_6_characteristic;

} NodeControlCtx;

static NodeControlCtx node_ctx;

typedef union _data {
  float f;
  char  s[4];
} myData;

myData q;


static void ready(void);

//------------------------------------------------------------------------------
// BLE Handlers

static void descriptor_write_handler(BLEDescriptor descriptor,
                                     BLEGATTError error) {
  char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
  Uuid descriptor_uuid = ble_descriptor_get_uuid(descriptor);
  uuid_to_string(&descriptor_uuid, uuid_buffer);

  APP_LOG(APP_LOG_LEVEL_INFO, "Write response for Descriptor %s (error=%u)", uuid_buffer, error);
}

static void descriptor_read_handler(BLEDescriptor descriptor,
                                    const uint8_t *value,
                                    size_t value_length,
                                    uint16_t value_offset,
                                    BLEGATTError error) {
  char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
  Uuid descriptor_uuid = ble_descriptor_get_uuid(descriptor);
  uuid_to_string(&descriptor_uuid, uuid_buffer);

  APP_LOG(APP_LOG_LEVEL_INFO, "Read Descriptor %s, %u bytes, error: %u", uuid_buffer, value_length, error);
  for (size_t i = 0; i < value_length; ++i) {
    APP_LOG(APP_LOG_LEVEL_INFO, "0x%02x", value[i]);
  }
}

static void process_read_buffer() {
  for (size_t i = 0; i < node_ctx.read_node_buffer_pos; ++i) {
		switch(node_ctx.read_node_buffer[i]) {
			case 0x04:
		  	APP_LOG(APP_LOG_LEVEL_INFO, "Incoming data for: CLIMA");
				if(node_ctx.read_node_buffer[i+1] == 0x0) { // temp & presssure
					uint32_t temp = (node_ctx.read_node_buffer[i+2] | node_ctx.read_node_buffer[i+3] << 8 
														| node_ctx.read_node_buffer[i+4] << 16 | node_ctx.read_node_buffer[i+5] << 24) / 10;
														
					uint32_t pressure = (node_ctx.read_node_buffer[i+6] | node_ctx.read_node_buffer[i+7] << 8 
														| node_ctx.read_node_buffer[i+8] << 16 | node_ctx.read_node_buffer[i+9] << 24) / 10000;

					APP_LOG(APP_LOG_LEVEL_INFO, "CLIMA temp: %lu C", temp);
					

					snprintf(node_ctx.temp_text_buffer, sizeof(node_ctx.temp_text_buffer), "%lu C", temp);
					text_layer_set_text(node_ctx.s_textlayer_temp, node_ctx.temp_text_buffer);
					APP_LOG(APP_LOG_LEVEL_INFO, "CLIMA pressure: %lu kPa", pressure);
					snprintf(node_ctx.pressure_text_buffer, sizeof(node_ctx.pressure_text_buffer), "%lu kPa", pressure);
					text_layer_set_text(node_ctx.s_textlayer_pressure, node_ctx.pressure_text_buffer);
				
				}
				else if(node_ctx.read_node_buffer[i+1] == 0x1) { // humidity
					int humidity = (node_ctx.read_node_buffer[i+2] | node_ctx.read_node_buffer[i+3] << 8) / 10;


					snprintf(node_ctx.humidity_text_buffer, sizeof(node_ctx.humidity_text_buffer), "%u rH%%", humidity);
					text_layer_set_text(node_ctx.s_textlayer_humidity, node_ctx.humidity_text_buffer);
					APP_LOG(APP_LOG_LEVEL_INFO, "CLIMA HUMIDITY: %u rH%%", humidity);
				}
				else if(node_ctx.read_node_buffer[i+1] == 0x3) { // light
// 					long lum = (node_ctx.read_node_buffer[i+2] | 
// 													node_ctx.read_node_buffer[i+3] << 8 | 
// 													node_ctx.read_node_buffer[i+4] << 16 | 
// 													node_ctx.read_node_buffer[i+5])<< 24;
// 					snprintf(node_ctx.info_text_buffer, sizeof(node_ctx.info_text_buffer), "Light: %lu.%03lu lx", lum, lum);
// 					text_layer_set_text(node_ctx.s_textlayer_status, node_ctx.info_text_buffer);
// 					APP_LOG(APP_LOG_LEVEL_INFO, "Light: %lu.%03lu lx", lum, lum);
				}
				else {
					APP_LOG(APP_LOG_LEVEL_INFO, "Invalid sensor data for CLIMA");
				}
				break;
			default:
				break;
	  }
	}
}

static void read_handler(BLECharacteristic characteristic,
                         const uint8_t *value,
                         size_t value_length,
                         uint16_t value_offset,
                         BLEGATTError error) {
  char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
  Uuid characteristic_uuid = ble_characteristic_get_uuid(characteristic);
  uuid_to_string(&characteristic_uuid, uuid_buffer);

  APP_LOG(APP_LOG_LEVEL_INFO, "Read Characteristic %s, %u bytes, error: %u", uuid_buffer, value_length, error);
  for (size_t i = 0; i < value_length; ++i) {
    APP_LOG(APP_LOG_LEVEL_INFO, "0x%02x", value[i]);
  }
  
	const Uuid node_service_4_uuid =
				UuidMake(0x18, 0xcd, 0xa7, 0x84, 0x4b, 0xd3, 0x43, 0x70,
								 0x85, 0xbb, 0xbf, 0xed, 0x91, 0xec, 0x86, 0xaf);

  // node sensor data incoming
  if (uuid_equal(&characteristic_uuid, &node_service_4_uuid)) {
  	APP_LOG(APP_LOG_LEVEL_INFO, "Incoming sensor data...");
  	if(value_length + node_ctx.read_node_buffer_pos > node_ctx.read_node_buffer_max) {
  		process_read_buffer();
  		node_ctx.read_node_buffer_pos = 0;
  	}
  	char *buff = (char *)&node_ctx.read_node_buffer; 
 		memcpy(buff+node_ctx.read_node_buffer_pos, value, value_length);  	
  	node_ctx.read_node_buffer_pos += value_length;
  }
  
}

static void write_handler(BLECharacteristic characteristic,
                          BLEGATTError error) {
  char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
  Uuid characteristic_uuid = ble_characteristic_get_uuid(characteristic);
  uuid_to_string(&characteristic_uuid, uuid_buffer);

  APP_LOG(APP_LOG_LEVEL_INFO, "Write response for Characteristic %s (error=%u)", uuid_buffer, error);
}

static void subscribe_handler(BLECharacteristic characteristic,
                              BLESubscription subscription_type,
                              BLEGATTError error) {

  char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
  Uuid characteristic_uuid = ble_characteristic_get_uuid(characteristic);
  uuid_to_string(&characteristic_uuid, uuid_buffer);

  APP_LOG(APP_LOG_LEVEL_INFO, "Subscription to Characteristic %s (subscription_type=%u, error=%u)",
          uuid_buffer, subscription_type, error);
}

static void service_change_handler(BTDevice device,
                                   const BLEService services[],
                                   uint8_t num_services,
                                   BTErrno status) {

	// out with the old...
  node_ctx.node_service_1_characteristic = BLE_CHARACTERISTIC_INVALID;
  node_ctx.node_command_characteristic = BLE_CHARACTERISTIC_INVALID;
  node_ctx.node_service_4_characteristic = BLE_CHARACTERISTIC_INVALID;
  node_ctx.node_service_6_characteristic = BLE_CHARACTERISTIC_INVALID;
  
  for (uint8_t i = 0; i < num_services; ++i) {
    Uuid service_uuid = ble_service_get_uuid(services[i]);

		const Uuid node_service_uuid = 
								UuidMake(0xda, 0x2b, 0x84, 0xf1, 0x62, 0x79, 0x48, 0xde,
												 0xbd, 0xc0, 0xaf, 0xbe, 0xa0, 0x22, 0x60, 0x79);

    if (!uuid_equal(&service_uuid, &node_service_uuid)) {
      // Not the Bean's "Scratch Service"
      continue;
    }
    
    char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
    uuid_to_string(&service_uuid, uuid_buffer);
    const BTDeviceAddress address = bt_device_get_address(device);
    APP_LOG(APP_LOG_LEVEL_INFO,
            "Discovered Node+ service %s (0x%08x) on " BT_DEVICE_ADDRESS_FMT,
            uuid_buffer,
            services[i],
            BT_DEVICE_ADDRESS_XPLODE(address));

    // Iterate over the characteristics within the "Scratch Service":
    BLECharacteristic characteristics[6];
    uint8_t num_characteristics =
    ble_service_get_characteristics(services[i], characteristics, 8);
    if (num_characteristics > 6) {
      num_characteristics = 6;
    }
    for (uint8_t c = 0; c < num_characteristics; ++c) {
      Uuid characteristic_uuid = ble_characteristic_get_uuid(characteristics[c]);

      // The characteristic UUIDs we're looking for:
      const Uuid node_service_1_uuid =
            UuidMake(0xa8, 0x79, 0x88, 0xb9, 0x69, 0x4c, 0x47, 0x9c,
                     0x90, 0x0e, 0x95, 0xdf, 0xa6, 0xc0, 0x0a, 0x24);
      const Uuid node_command_uuid =
            UuidMake(0xbf, 0x03, 0x26, 0x0c, 0x72, 0x05, 0x4c, 0x25,
            				 0xaf, 0x43, 0x93, 0xb1, 0xc2, 0x99, 0xd1, 0x59);
      const Uuid node_service_4_uuid =
            UuidMake(0x18, 0xcd, 0xa7, 0x84, 0x4b, 0xd3, 0x43, 0x70,
            				 0x85, 0xbb, 0xbf, 0xed, 0x91, 0xec, 0x86, 0xaf);
      const Uuid node_service_6_uuid =
            UuidMake(0xfd, 0xd6, 0xb4, 0xd3, 0x04, 0x6d, 0x43, 0x30, 
            				 0xbd, 0xec, 0x1f, 0xd0, 0xc9, 0x0c, 0xb4, 0x3b);

      uint8_t node_num = 0; // Just for logging purposes
      if (uuid_equal(&characteristic_uuid, &node_service_1_uuid)) {
        // Found node service 1
        node_ctx.node_service_1_characteristic = characteristics[c];
        node_num = 1;
      } else if (uuid_equal(&characteristic_uuid, &node_command_uuid)) {
        // Found node command 
        node_ctx.node_command_characteristic = characteristics[c];
        node_num = 2;
      } else if (uuid_equal(&characteristic_uuid, &node_service_4_uuid)) {
        // Found node command 
        node_ctx.node_service_4_characteristic = characteristics[c];
        node_num = 3;
      } else if (uuid_equal(&characteristic_uuid, &node_service_6_uuid)) {
        // Found node command 
        node_ctx.node_service_6_characteristic = characteristics[c];
        node_num = 4;
      } else {
        continue;
      }

      uuid_to_string(&characteristic_uuid, uuid_buffer);
      APP_LOG(APP_LOG_LEVEL_INFO, "-- Found %u: %s (0x%08x)",
              node_num, uuid_buffer, characteristics[c]);

      // Check if all characteristics are found
      if (node_ctx.node_service_1_characteristic != BLE_CHARACTERISTIC_INVALID &&
          node_ctx.node_command_characteristic != BLE_CHARACTERISTIC_INVALID &&
          node_ctx.node_service_4_characteristic != BLE_CHARACTERISTIC_INVALID &&
          node_ctx.node_service_6_characteristic != BLE_CHARACTERISTIC_INVALID) {
          
          ready();
      }
    }
  }
}

static void connection_handler(BTDevice device, BTErrno connection_status) {
  const BTDeviceAddress address = bt_device_get_address(device);

  const bool connected = (connection_status == BTErrnoConnected);

  APP_LOG(APP_LOG_LEVEL_INFO, "%s " BT_DEVICE_ADDRESS_FMT " (status=%d)",
          connected ? "Connected" : "Disconnected",
          BT_DEVICE_ADDRESS_XPLODE(address), connection_status);

  ble_client_discover_services_and_characteristics(device);
}

//------------------------------------------------------------------------------
// Node Helpers:

static void enableClima() {
	if(node_ctx.node_command_characteristic != BLE_CHARACTERISTIC_INVALID) {
		APP_LOG(APP_LOG_LEVEL_INFO, "-- Enabling Clima ");

		const char enableCmd[] = "$CLIMA,1,1,1,1,0$";
		BTErrno e = ble_client_write(node_ctx.node_command_characteristic,
																			 (const uint8_t *) &enableCmd, 
																			 sizeof(enableCmd));
		if (e) {
			APP_LOG(APP_LOG_LEVEL_INFO, "-- FAILED to Enabled Clima ");
		}
		else {
			APP_LOG(APP_LOG_LEVEL_INFO, "-- Enabled Clima ");
		}
	}

}

static void disableClima() {
	if(node_ctx.node_command_characteristic != BLE_CHARACTERISTIC_INVALID) {
		APP_LOG(APP_LOG_LEVEL_INFO, "-- Disabling Clima ");

	  snprintf(node_ctx.node_command_buffer, sizeof(node_ctx.node_command_buffer), "$%s,0$", "CLIMA,0,0,0,1");
  	BTErrno e = ble_client_write(node_ctx.node_command_characteristic,
                                     (const uint8_t *) &node_ctx.node_command_buffer, 
                                     sizeof(node_ctx.node_command_buffer));
		if (e) {
			APP_LOG(APP_LOG_LEVEL_INFO, "-- FAILED to Disable Clima ");
		} 
		else {
			APP_LOG(APP_LOG_LEVEL_INFO, "-- Disabled Clima ");
		}
	}
}

void ble_node_control_set_device(BTDevice device) {
  node_ctx.device = device;
}


static void connect(void) {
  BTErrno e = ble_central_connect(node_ctx.device,
                                  true /* auto_reconnect */,
                                  false /* is_pairing_required */);
  if (e) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Error Connecting: %d", e);
  } else {
    text_layer_set_text(node_ctx.s_textlayer_status, "connecting...");
  }
}

static void disconnect(void) {
	disableClima();
  BTErrno e = ble_central_cancel_connect(node_ctx.device);
  if (e) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Error Connecting: %d", e);
  } else {
    text_layer_set_text(node_ctx.s_textlayer_status, "disconnecting...");
  }
}

static void ready(void) {
  const BTDeviceAddress address = bt_device_get_address(node_ctx.device);
  snprintf(node_ctx.info_text_buffer, sizeof(node_ctx.info_text_buffer), "connected to node");
  text_layer_set_text(node_ctx.s_textlayer_status, node_ctx.info_text_buffer);
	vibes_double_pulse();
		/*
		  	BTErrno ble_client_subscribe(BLECharacteristic characteristic,
                                 BLESubscription subscription_type);
		*/
  
	if(ble_characteristic_is_notifiable(node_ctx.node_service_6_characteristic)) {
      APP_LOG(APP_LOG_LEVEL_INFO, "-- Subscribing to service 6 ");
			BTErrno e = ble_client_subscribe(node_ctx.node_service_6_characteristic, 
																			  BLESubscriptionNotifications);
			if (e) {
      	APP_LOG(APP_LOG_LEVEL_ERROR, "-- FAILED to subscribe to service 6, ERR = %d ", e);
			} else {
      	APP_LOG(APP_LOG_LEVEL_INFO, "-- Subscribed to service 6 ");
				if(ble_characteristic_is_indicatable(node_ctx.node_service_4_characteristic)) {
						APP_LOG(APP_LOG_LEVEL_INFO, "-- Subscribing to service 4 ");
						BTErrno e = ble_client_subscribe(node_ctx.node_service_4_characteristic, 
																							BLESubscriptionIndications);
						if (e) {
							APP_LOG(APP_LOG_LEVEL_ERROR, "-- FAILED to subscribe to service 4, ERR = %d ", e);
						} else {
							APP_LOG(APP_LOG_LEVEL_INFO, "-- Subscribed to service 4 ");
							e = ble_client_read(node_ctx.node_service_6_characteristic);
							uint8_t buff[] = {0x01};
							e = ble_client_write(node_ctx.node_service_1_characteristic,
																	 (const uint8_t *) &buff, 
																	 sizeof(buff));
							e = ble_client_write(node_ctx.node_service_1_characteristic,
																	 (const uint8_t *) &buff, 
																	 sizeof(buff));
						  e = ble_client_read(node_ctx.node_service_1_characteristic);
							e = ble_client_read(node_ctx.node_service_1_characteristic);

						}		
				}
      }		
	}

	
	
}

//------------------------------------------------------------------------------
// Window callbacks:

static void window_load(Window *s_window) {
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);

  //node_ctx.s_textlayer_status = text_layer_create(bounds);
  //layer_add_child(window_layer, text_layer_get_layer(node_ctx.s_textlayer_status));

  s_res_gothic_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  s_res_gothic_14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  s_res_gothic_14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_res_bitham_30_black = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);



  // s_textlayer_temp
  node_ctx.s_textlayer_temp = text_layer_create(GRect(5, 100, 135, 42));
  text_layer_set_text(node_ctx.s_textlayer_temp, " ");
  text_layer_set_font(node_ctx.s_textlayer_temp, s_res_bitham_30_black);
  layer_add_child(window_get_root_layer(s_window), text_layer_get_layer(node_ctx.s_textlayer_temp));
  
  // s_textlayer_humidity
  node_ctx.s_textlayer_humidity = text_layer_create(GRect(5, 20, 135, 42));
  text_layer_set_text(node_ctx.s_textlayer_humidity, " ");
  text_layer_set_font(node_ctx.s_textlayer_humidity, s_res_bitham_30_black);
  layer_add_child(window_get_root_layer(s_window), text_layer_get_layer(node_ctx.s_textlayer_humidity));
  
  // s_textlayer_pressure
  node_ctx.s_textlayer_pressure = text_layer_create(GRect(5, 60, 135, 42));
  text_layer_set_text(node_ctx.s_textlayer_pressure, " ");
  text_layer_set_font(node_ctx.s_textlayer_pressure, s_res_bitham_30_black);
  layer_add_child(window_get_root_layer(s_window), text_layer_get_layer(node_ctx.s_textlayer_pressure));
  
  // s_textlayer_status
  node_ctx.s_textlayer_status = text_layer_create(GRect(5, 2, 135, 20));
  text_layer_set_text(node_ctx.s_textlayer_status, "Environator");
  text_layer_set_font(node_ctx.s_textlayer_status, s_res_gothic_14);
  layer_add_child(window_get_root_layer(s_window), text_layer_get_layer(node_ctx.s_textlayer_status));


  // Set up handlers:
  ble_client_set_descriptor_write_handler(descriptor_write_handler);
  ble_client_set_descriptor_read_handler(descriptor_read_handler);
  ble_client_set_read_handler(read_handler);
  ble_client_set_write_response_handler(write_handler);
  ble_client_set_subscribe_handler(subscribe_handler);
  ble_central_set_connection_handler(connection_handler);
  ble_client_set_service_change_handler(service_change_handler);
  
	node_ctx.read_node_buffer_pos = 0;
	node_ctx.read_node_buffer_max = 32;
  
  
}

static void window_unload(Window *window) {
//   text_layer_destroy(node_ctx.s_textlayer_status);
//   node_ctx.s_textlayer_status = NULL;

  text_layer_destroy(node_ctx.s_textlayer_temp);
  text_layer_destroy(node_ctx.s_textlayer_humidity);
  text_layer_destroy(node_ctx.s_textlayer_pressure);
  text_layer_destroy(node_ctx.s_textlayer_status);

  window_destroy(window);
  node_ctx.window = NULL;
}

static void window_disappear(Window *window) {
  disconnect();
}

static void window_appear(Window *window) {
  connect();
}

// Buttons

// static void handle_up_button_up(ClickRecognizerRef recognizer, void *context) {
// 
//   printf("UP=0");
// }
// 

static void handle_up_button_down(ClickRecognizerRef recognizer, void *context) {
  printf("UP=1");
  enableClima();
}

// static void handle_down_button_up(ClickRecognizerRef recognizer, void *context) {
//   printf("DOWN=0");
// }
// 
// static void handle_down_button_down(ClickRecognizerRef recognizer, void *context) {
//   printf("DOWN=1");
// }
// 
static void click_config_provider(void *data) {

	window_single_click_subscribe(BUTTON_ID_UP, handle_up_button_down);
//   window_raw_click_subscribe(BUTTON_ID_UP, handle_up_button_down, handle_up_button_up, NULL);
//   window_raw_click_subscribe(BUTTON_ID_DOWN, handle_down_button_down, handle_down_button_up, NULL);
}


//------------------------------------------------------------------------------

Window * ble_node_control_window_create(void) {

  Window *window = window_create();
  window_set_user_data(window, &node_ctx);

  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
    .appear = window_appear,
    .disappear = window_disappear,
  });

  window_set_click_config_provider(window, click_config_provider);

  node_ctx.window = window;
  return window;
}
