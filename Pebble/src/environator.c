#include "ble_node.h"

static Window *s_scan_window;


int main(void) {


  s_scan_window = ble_node_window_create();

  window_stack_push(s_scan_window, true /* Animated */);

  app_event_loop();

  window_destroy(s_scan_window);
}
