#include <pebble.h>

static Window *s_main_window;

static Layer *window_layer;
static Layer *background_layer;
static Layer *minute_layer;
static Layer *hour_layer;

static GBitmap *hour_bitmap = NULL;
static GBitmap *minute_bitmap = NULL;
static GBitmap *background_bitmap = NULL;
static GRect window_frame;
static int batt_percent;

/**
 * Unload a bitmap and layer.
 */
static void unload_layer(Layer **layer, GBitmap **bitmap) {
  if (*layer) {
    layer_destroy(*layer);
    layer = NULL;
  }
  if (*bitmap) {
    gbitmap_destroy(*bitmap);
    bitmap = NULL;
  }
}

/**
 * Called when the battery level changes.
 */
static void handle_battery(BatteryChargeState charge_state) {
  batt_percent = charge_state.charge_percent;
  layer_mark_dirty(background_layer);
}

/**
 * Redraw the time UI elements
 */
static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  int hour = tick_time->tm_hour % 12;
  int min = tick_time->tm_min;

  int angle = TRIG_MAX_ANGLE * hour / 12 + TRIG_MAX_ANGLE * min / 12 / 60 +
              TRIG_MAX_ANGLE * 3 / 4;
  int hour_cx =
      cos_lookup(angle) * 41 / TRIG_MAX_RATIO + window_frame.size.w / 2;
  int hour_cy =
      sin_lookup(angle) * 41 / TRIG_MAX_RATIO + window_frame.size.h / 2;

  angle = TRIG_MAX_ANGLE * min / 60 + TRIG_MAX_ANGLE * 3 / 4;
  int min_cx = cos_lookup(angle) * 20 / TRIG_MAX_RATIO + hour_cx;
  int min_cy = sin_lookup(angle) * 20 / TRIG_MAX_RATIO + hour_cy;

  layer_set_frame(hour_layer, GRect(hour_cx - 20, hour_cy - 20, 42, 42));
  layer_set_frame(minute_layer, GRect(min_cx - 7, min_cy - 7, 15, 15));
}

static void background_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_frame(layer);
  graphics_context_set_stroke_color(
      ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_bitmap_in_rect(
      ctx, background_bitmap,
      GRect(bounds.size.w / 2 - 12, bounds.size.h / 2 - 12, 24, 24));
  graphics_draw_arc(
      ctx, GRect(bounds.size.w / 2 - 41, bounds.size.h / 2 - 41, 83, 83),
      GOvalScaleModeFitCircle, TRIG_MAX_ANGLE * (100 - batt_percent) / 100,
      TRIG_MAX_ANGLE);
}

static void hour_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_frame(layer);
  GRect bmap_bounds =
      GRect(bounds.size.w / 2 - 7, bounds.size.h / 2 - 7, 15, 15);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 6);
  graphics_draw_arc(ctx, GRect(4, 4, bounds.size.w - 4, bounds.size.h - 4),
                    GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(ctx,
                    GRect(bmap_bounds.origin.x - 2, bmap_bounds.origin.y - 2,
                          bmap_bounds.size.w + 4, bmap_bounds.size.h + 4),
                    GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  graphics_context_set_stroke_color(
      ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(
      ctx, GRect(bounds.size.w / 2 - 20, bounds.size.h / 2 - 20, 40, 40),
      GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  graphics_draw_bitmap_in_rect(ctx, hour_bitmap, bmap_bounds);
}

static void minute_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_frame(layer);
  GRect bmap_bounds =
      GRect(bounds.size.w / 2 - 5, bounds.size.h / 2 - 5, 11, 11);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_arc(ctx, GRect(1, 1, bounds.size.w - 2, bounds.size.h - 2),
                    GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  graphics_draw_bitmap_in_rect(ctx, minute_bitmap, bmap_bounds);
}

static void main_window_load(Window *window) {
  window_layer = window_get_root_layer(window);
  window_frame = layer_get_frame(window_layer);

  background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SUN);
  hour_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_EARTH);
  minute_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON);

  background_layer = layer_create(window_frame);
  layer_set_update_proc(background_layer, background_update_proc);

  hour_layer = layer_create(window_frame);
  layer_set_update_proc(hour_layer, hour_update_proc);

  minute_layer = layer_create(window_frame);
  layer_set_update_proc(minute_layer, minute_update_proc);

  layer_add_child(window_layer, background_layer);
  layer_add_child(window_layer, hour_layer);
  layer_add_child(window_layer, minute_layer);

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  battery_state_service_subscribe(handle_battery);

  // Ensures time is displayed immediately (will break if NULL tick event
  // accessed). (This is why it's a good idea to have a separate routine to
  // do the update itself.)
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  handle_minute_tick(current_time, MINUTE_UNIT);

  handle_battery(battery_state_service_peek());
}

static void main_window_unload(Window *window) {
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  unload_layer(&minute_layer, &minute_bitmap);
  unload_layer(&hour_layer, &hour_bitmap);
  unload_layer(&background_layer, &background_bitmap);
}

static void init() {
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
                                                .load = main_window_load,
                                                .unload = main_window_unload,
                                            });
  window_stack_push(s_main_window, true);
}

static void deinit() { window_destroy(s_main_window); }

int main(void) {
  init();
  app_event_loop();
  deinit();
}
