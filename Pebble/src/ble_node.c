#include "ble_node.h"
#include "ble_node_control.h"


#define NODE_DEVICE_NAME	"NODE-"

static MenuLayer *s_menu_layer;

struct ScanResult;

typedef struct ScanResult {
  struct ScanResult *next;
  BTDevice device;
  int8_t rssi;
  int8_t tx_power_level;
  char local_name[32];
  bool has_services;
  bool has_node_service;
  Uuid first_service_uuid;
} ScanResult;

static bool s_is_scanning;

//------------------------------------------------------------------------------
// ScanResult List management

static ScanResult *s_head;

//! Gets the number of ScanResults in the list:
static uint8_t list_get_count(void) {
  uint8_t count = 0;
  ScanResult *result = s_head;
  while (result) {
    ++count;
    result = result->next;
  }
  return count;
}

static void list_free_last(void) {
  ScanResult *prev = NULL;
  ScanResult *result = s_head;
  while (result) {
    if (!result->next) {
      // Found the last result, unlink and free it:
      prev->next = NULL;
      free(result);
      return;
    }
    prev = result;
    result = result->next;
  }
}

//! Finds ScanResult based on BTDevice. If found, unlink and return ScanResult.
static ScanResult *list_unlink(const BTDevice *device) {
  ScanResult *prev = NULL;
  ScanResult *result = s_head;
  while (result) {
    if (bt_device_equal(&result->device, device)) {
      // Match!
      if (prev) {
        // Unlink from previous node:
        prev->next = result->next;
      } else {
        // Unlink from head:
        s_head = result->next;
      }
      // Return found result:
      return result;
    }

    // Iterate:
    prev = result;
    result = result->next;
  }
  // Not found:
  return NULL;
}

//! Inserts the result into the list, keeping the list sorted by RSSI (strongest
//! first).
static void list_link_sorted_by_rssi(ScanResult *result) {
  ScanResult *prev = NULL;
  ScanResult *other = s_head;
  while (other) {
    if (other->rssi < result->rssi) {
      if (!prev) {
        // Need to insert as the head, this is handled at the end.
        break;
      }
      // Insert before "other":
      prev->next = result;
      result->next = other;
      return;
    }

    // Iterate:
    prev = other;
    other = other->next;
  }

  // Broke out of the loop, this can happen at the head or the tail, handle it:
  if (s_head == other) {
    // Insert as head of the list:
    result->next = s_head;
    s_head = result;
  } else {
    // Insert as tail:
    prev->next = result;
    result->next = NULL;
  }
}

static ScanResult *list_get_by_index(uint8_t index) {
  ScanResult *result = s_head;
  while (index && result) {
    result = result->next;
    --index;
  }
  return result;
}

static void list_free_all(void) {
  ScanResult *result = s_head;
  while (result) {
    ScanResult *next = result->next;
    free(result);
    result = next;
  }
  s_head = NULL;
}

//------------------------------------------------------------------------------
// BLE Scan API callback

/*
[INFO    ] I ble_node.c:139 Got Advertisement from: EC:FE:7E:12:D2:DC
[INFO    ] I ble_node.c:167 TX Power: 10
[INFO    ] I ble_node.c:174 Local Name: NODE-2D95AFA57C45
*/

static void ble_scan_handler(BTDevice device,
                             int8_t rssi,
                             const BLEAdData *ad_data) {

  char temp_local_name[32] = {0};
  ble_ad_copy_local_name(ad_data, temp_local_name,
                         sizeof(temp_local_name));
  if (strncmp(temp_local_name, NODE_DEVICE_NAME, 5) != 0) {
    // If the Local Name of the device is not "pebblebot",
    // don't show it in the menu.
    return;
  }

  const BTDeviceAddress address = bt_device_get_address(device);
  APP_LOG(APP_LOG_LEVEL_INFO, "Got Advertisement from: " BT_DEVICE_ADDRESS_FMT,
          BT_DEVICE_ADDRESS_XPLODE(address));

  // Find existing ScanResult with BTDevice:
  ScanResult *result = list_unlink(&device);

  // If no existing result, create one:
  if (!result) {
    // Bound the number of items:
    if (list_get_count() >= 10) {
      list_free_last();
    }
    // Create new ScanResult:
    result = (ScanResult *) malloc(sizeof(ScanResult));
    if (!result) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Out of memory!");
      return;
    }
    // Zero out:
    memset(result, 0, sizeof(ScanResult));
  }

  // Update all the fields into the result:
  result->device = device;
  result->rssi = rssi;

  // Try getting TX Power Level:
  int8_t tx_power_level;
  if (ble_ad_get_tx_power_level(ad_data, &tx_power_level)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "TX Power: %d", tx_power_level);
    result->tx_power_level = tx_power_level;
  }

  // Try getting Local Name:
  if (ble_ad_copy_local_name(ad_data, result->local_name,
                             sizeof(result->local_name))) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Local Name: %s", result->local_name);
  } else {
    // Clear out the local name field:
    result->local_name[0] = 0;
  }

  // Try to copy the first Service UUID, we'll display this in the list:
  const uint8_t num_services = ble_ad_copy_service_uuids(ad_data,
                                                         &result->first_service_uuid, 1);
  if (num_services) {
    result->has_services = true;

		const Uuid node_service_uuid = UuidMake(0xda, 0x2b, 0x84, 0xf1, 0x62, 0x79, 0x48, 0xde, 0xbd, 0xc0, 0xaf, 0xbe, 0xa0, 0x22, 0x60, 0x79);

    // Look for Heart Rate Monitor service:
    // See https://developer.bluetooth.org/gatt/services/Pages/ServicesHome.aspx
    result->has_node_service = ble_ad_includes_service(ad_data, &node_service_uuid);
  } else {
    result->has_services = false;
    result->has_node_service = false;
  }

  // Insert into the list:
  list_link_sorted_by_rssi(result);

  // Tell the menu to update:
  menu_layer_reload_data(s_menu_layer);
}

void toggle_scan(void) {
  if (s_is_scanning) {
    ble_scan_stop();
    s_is_scanning = false;
  } else {
    ble_scan_start(ble_scan_handler);
    s_is_scanning = true;
  }
  menu_layer_reload_data(s_menu_layer);
}

//------------------------------------------------------------------------------
// MenuLayer callbacks:

enum {
  SectionControl = 0,
  SectionData,
};

static uint16_t menu_get_num_sections_callback(struct MenuLayer *menu_layer,
                 void *callback_context) {
  return 2;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer,
                                           uint16_t section_index, void *data) {
  switch (section_index) {
    case SectionControl:
      return 1;
    case SectionData:
      return list_get_count();
    default:
      return 0;
  }
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer,
                                               uint16_t section_index,
                                               void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer,
                                      uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer,
                              (section_index == SectionData) ? "Results" : "Options");
}

static void draw_data_row(GContext* ctx, const Layer *cell_layer,
                          MenuIndex *cell_index, void *data) {
  ScanResult *result = list_get_by_index(cell_index->row);

  // Build the title string:
  char title[32];
  // Annotate with "NODE" if the device has a heart rate service:
  char *node_str = result->has_node_service ? "NODE" : "";
  // If there is a local name, show it, otherwise use the device address:
  if (strlen(result->local_name)) {
    snprintf(title, sizeof(title), "%s %s", result->local_name, node_str);
  } else {
    const BTDeviceAddress address = bt_device_get_address(result->device);
    snprintf(title, sizeof(title), BT_DEVICE_ADDRESS_FMT " %s",
             BT_DEVICE_ADDRESS_XPLODE(address), node_str);
  }

  // Build the subtitle string:
  char subtitle[UUID_STRING_BUFFER_LENGTH];
  if (result->has_services) {
    // Make a displayable string of the first Service UUID:
    uuid_to_string(&result->first_service_uuid, subtitle);
  } else {
    // If advertisement did not contain Service UUIDs:
    strncpy(subtitle, "No Service UUIDs", sizeof(subtitle));
  }

  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer,
                                   MenuIndex *cell_index, void *data) {
  switch (cell_index->section) {
    case SectionControl:
      menu_cell_basic_draw(ctx, cell_layer,
                           s_is_scanning ? "Disable Scan" : "Enable Scan",
                           NULL, NULL);
      break;
    case SectionData:
      draw_data_row(ctx, cell_layer, cell_index, data);
      break;
    default:
      break;
  }
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index,
                                 void *data) {
  if (cell_index->section == SectionControl) {
    toggle_scan();
    return;
  }

  // Select device:
  ScanResult *result = list_get_by_index(cell_index->row);
  ble_node_control_set_device(result->device);
  window_stack_push(ble_node_control_window_create(), true /* animated */);
}

//------------------------------------------------------------------------------
// Window callbacks:

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  window_set_user_data(window, s_menu_layer);

  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
  });

  menu_layer_set_click_config_onto_window(s_menu_layer, window);

  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  // Start scanning. Advertisments will be delivered in the callback.
  toggle_scan();
}

static void window_unload(Window *window) {
  // After ble_scan_stop() returns, the scan handler will not get called again.
  ble_scan_stop();
  s_is_scanning = false;

  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;

  list_free_all();
}

//------------------------------------------------------------------------------

Window * ble_node_window_create(void) {

  Window * window = window_create();

  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  return window;
}
