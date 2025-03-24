#include <pebble.h>

static int earth_orbit_radius;
static int moon_orbit_radius;

#define SUN_RADIUS 12
#define EARTH_RADIUS 7
#define MOON_RADIUS 5
#define RAD_TO_DIA(rad) (rad * 2 + 1)

static Window *s_main_window;

static Layer *window_layer;
static Layer *s_layer;

static GBitmap *hour_bitmap = NULL;
static GBitmap *minute_bitmap = NULL;
static GBitmap *background_bitmap = NULL;
static GRect window_frame;
static int batt_percent;
static int hour, min;

/**
 * Called when the battery level changes.
 */
static void handle_battery(BatteryChargeState charge_state) {
  batt_percent = charge_state.charge_percent;
  layer_mark_dirty(s_layer);
}

/**
 * Redraw the time UI elements
 */
static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  if (units_changed & MINUTE_UNIT) {
    hour = tick_time->tm_hour % 12;
    min = tick_time->tm_min;
    layer_mark_dirty(s_layer);
  }
}

static void byte_set_bit(uint8_t *byte, uint8_t bit, uint8_t value) {
  *byte ^= (-value ^ *byte) & (1 << bit);
}

static void set_pixel_color(GBitmapDataRowInfo info, GPoint point,
                            uint8_t color) {
  uint8_t byte = point.x / 8;
  uint8_t bit = point.x % 8;
  byte_set_bit(&info.data[byte], bit, color);
}

static void background_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_frame(layer);

  graphics_context_set_stroke_color(
      ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(
      ctx,
      GRect(bounds.size.w / 2 - earth_orbit_radius,
            bounds.size.h / 2 - earth_orbit_radius,
            RAD_TO_DIA(earth_orbit_radius), RAD_TO_DIA(earth_orbit_radius)),
      GOvalScaleModeFitCircle, TRIG_MAX_ANGLE * (100 - batt_percent) / 100,
      TRIG_MAX_ANGLE);

  int angle = TRIG_MAX_ANGLE * hour / 12 + TRIG_MAX_ANGLE * min / 12 / 60 +
              TRIG_MAX_ANGLE * 3 / 4;
  int hour_cx = cos_lookup(angle) * earth_orbit_radius / TRIG_MAX_RATIO +
                window_frame.size.w / 2;
  int hour_cy = sin_lookup(angle) * earth_orbit_radius / TRIG_MAX_RATIO +
                window_frame.size.h / 2;

  angle = TRIG_MAX_ANGLE * min / 60 + TRIG_MAX_ANGLE * 3 / 4;
  int min_cx = cos_lookup(angle) * moon_orbit_radius / TRIG_MAX_RATIO + hour_cx;
  int min_cy = sin_lookup(angle) * moon_orbit_radius / TRIG_MAX_RATIO + hour_cy;

  // Moon orbit outline
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 6);
  graphics_draw_arc(
      ctx,
      GRect(hour_cx - moon_orbit_radius, hour_cy - moon_orbit_radius,
            RAD_TO_DIA(moon_orbit_radius), RAD_TO_DIA(moon_orbit_radius)),
      GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  // Moon orbit
  graphics_context_set_stroke_color(
      ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(
      ctx,
      GRect(hour_cx - moon_orbit_radius, hour_cy - moon_orbit_radius,
            RAD_TO_DIA(moon_orbit_radius), RAD_TO_DIA(moon_orbit_radius)),
      GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  // Earth outline
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 5);
  graphics_draw_arc(ctx,
                    GRect(hour_cx - EARTH_RADIUS, hour_cy - EARTH_RADIUS,
                          RAD_TO_DIA(EARTH_RADIUS), RAD_TO_DIA(EARTH_RADIUS)),
                    GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  // Moon outline
  graphics_draw_arc(ctx,
                    GRect(min_cx - MOON_RADIUS, min_cy - MOON_RADIUS,
                          RAD_TO_DIA(MOON_RADIUS), RAD_TO_DIA(MOON_RADIUS)),
                    GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

#ifdef PBL_BW
  // Dither
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  // Iterate over all rows
  for (int y = 0; y < bounds.size.h; y++) {
    // Get this row's range and data
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);

    // Iterate over all visible columns
    for (int x = info.min_x; x <= info.max_x; x++) {
      if ((x + y) % 2) {
        set_pixel_color(info, GPoint(x, y), 0);
      }
    }
  }
  graphics_release_frame_buffer(ctx, fb);
#endif

  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  // Earth
  graphics_draw_bitmap_in_rect(
      ctx, hour_bitmap,
      GRect(hour_cx - EARTH_RADIUS, hour_cy - EARTH_RADIUS,
            RAD_TO_DIA(EARTH_RADIUS), RAD_TO_DIA(EARTH_RADIUS)));
  graphics_draw_bitmap_in_rect(ctx, minute_bitmap,
                               GRect(min_cx - MOON_RADIUS, min_cy - MOON_RADIUS,
                                     RAD_TO_DIA(MOON_RADIUS),
                                     RAD_TO_DIA(MOON_RADIUS)));
  graphics_draw_bitmap_in_rect(
      ctx, background_bitmap,
      GRect(bounds.size.w / 2 - SUN_RADIUS, bounds.size.h / 2 - SUN_RADIUS,
            RAD_TO_DIA(SUN_RADIUS), RAD_TO_DIA(SUN_RADIUS)));
}

static void main_window_load(Window *window) {
  window_layer = window_get_root_layer(window);
  window_frame = layer_get_frame(window_layer);
  int min_dim = window_frame.size.w < window_frame.size.h ? window_frame.size.w
                                                          : window_frame.size.h;
  earth_orbit_radius = (min_dim + RAD_TO_DIA(SUN_RADIUS)) / 4;
  moon_orbit_radius = (min_dim + RAD_TO_DIA(EARTH_RADIUS)) / 8;

  background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SUN);
  hour_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_EARTH);
  minute_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON);

  s_layer = layer_create(window_frame);
  layer_set_update_proc(s_layer, background_update_proc);

  layer_add_child(window_layer, s_layer);

  // Ensures time is displayed immediately (will break if NULL tick event
  // accessed). (This is why it's a good idea to have a separate routine to
  // do the update itself.)
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  handle_minute_tick(current_time, MINUTE_UNIT);

  handle_battery(battery_state_service_peek());
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  battery_state_service_subscribe(handle_battery);
}

static void main_window_unload(Window *window) {
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  layer_destroy(s_layer);
  gbitmap_destroy(background_bitmap);
  gbitmap_destroy(hour_bitmap);
  gbitmap_destroy(minute_bitmap);
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
