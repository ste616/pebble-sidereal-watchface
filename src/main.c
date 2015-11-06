#include <pebble.h>

#define KEY_LONGITUDE 0

static Window *s_my_window;
static TextLayer *s_local_time, *s_local_label, *s_local_dst, *s_local_date;
static TextLayer *s_utc_label, *s_utc_time, *s_mjd;
static TextLayer *s_lst_label, *s_lst_time;

int num_text_layers = 0;
TextLayer **all_text_layers = NULL;

// An all-in-one section maker.
static void add_window_element(Layer *wlayer, TextLayer **tlayer, GColor bgcolour,
                               GColor fgcolour, GFont tfont, GTextAlignment talign,
                               float sx, float sy, float w, float h) {
  *tlayer = text_layer_create(GRect(sx, sy, w, h));
  text_layer_set_background_color(*tlayer, bgcolour);
  text_layer_set_text_color(*tlayer, fgcolour);
  text_layer_set_font(*tlayer, tfont);
  text_layer_set_text_alignment(*tlayer, talign);
  layer_add_child(wlayer, text_layer_get_layer(*tlayer));

  num_text_layers++;
  all_text_layers = realloc(all_text_layers, num_text_layers * sizeof(TextLayer *));
  all_text_layers[num_text_layers - 1] = *tlayer;
}

// Calculate the fraction of the day past midnight.
static double calc_day_fraction(struct tm *t) {
  double dayfrac = 0.0;
  dayfrac += (double)t->tm_hour + (double)t->tm_min / 60.0 + (double)t->tm_sec / 3600.0;
  dayfrac /= 24.0;
  return(dayfrac);
}

// Calculate the Modified Julian Date (MJD).
static double calc_mjd(struct tm *t) {
  double day_fraction = calc_day_fraction(t);
  double mjd;
  int m, y, c, yy, x1, x2, x3;

  if (t->tm_mon < 2) {
    m = t->tm_mon + 10;
    y = t->tm_year + 1900 - 1;
  } else {
    m = t->tm_mon - 2;
    y = t->tm_year + 1900;
  }

  yy = y % 100;
  c = (y - yy) / 100;
  x1 = (int)(146097.0 * (double)c / 4.0);
  x2 = (int)(1461.0 * (double)yy / 4.0);
  x3 = (int)((153.0 * (double)m + 2.0) / 5.0);
  
  mjd = (double)x1 + (double)x2 + (double)x3 + (double)t->tm_mday - 678882.0 + day_fraction;
  return(mjd);
}

// Calculate the sidereal time (GMST).
static double mjd2gmst(double mjd) {
  // The Julian date at the start of the epoch.
  double jdJ2000 = 2451545.0;
  
  // The number of days in a century.
  double jdCentury = 36525.0;
  
  double dUT1 = 0.0;

  double a = 101.0 + 24110.54841 / 86400.0;
  double b = 8640184.812866 / 86400.0;
  double e = 0.093104 / 86400.0;
  double d = 0.0000062 / 86400.0;
  
  double tu = ((double)((int)mjd) - (jdJ2000 - 2400000.5)) / jdCentury;
  double sidTim = a + tu * (b + tu * (e - tu * d));
  sidTim -= (double)((int)sidTim);
  if (sidTim < 0) {
    sidTim += 1;
  }
  
  double gmst = sidTim + (mjd - (double)((int)mjd) + dUT1 / 86400.0) * 1.002737909350795;
  while (gmst < 0) {
    gmst += 1;
  }
  while (gmst > 1) {
    gmst -= 1;
  }
  
  return(gmst);
}

// Calculate the sidereal time (LST).
static double gmst2lst(double gmst) {
  // Get the longitude from our configuration.
  double longitude;
  if (persist_exists(KEY_LONGITUDE)) { 
    persist_read_data(KEY_LONGITUDE, &longitude, sizeof(double));
  } else {
    longitude = 0.0; // 149.5501388 / 360.0; // ATCA longitude in turns.
  }
  longitude /= 360.0; // Convert degrees to turns.
  double lst = gmst + longitude;
  while (lst > 1) {
    lst -= 1;
  }
  while (lst < 0) {
    lst += 1;
  }
  return(lst * 24.0);
}

// Update the time segments.
static void update_time() {
  // Get a tm structure.
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);
  struct tm *utc_tick_time = gmtime(&temp);

  // Write the current hours and minutes into a buffer for each of the time types
  // we need to display.
  static char s_local_time_buffer[8], s_local_date_buffer[23], s_local_is_dst[4];
  static char s_utc_time_buffer[8], s_mjd_buffer[12], s_lst_time_buffer[8];
  double mjd_time = calc_mjd(utc_tick_time);
  double gmst_time = mjd2gmst(mjd_time);
  double lst_time = gmst2lst(gmst_time);
  int lst_hour = (int)lst_time;
  int lst_min = (int)((lst_time - (double)lst_hour) * 60);
  strftime(s_local_time_buffer, sizeof(s_local_time_buffer), "%H:%M", tick_time);
  strftime(s_local_date_buffer, sizeof(s_local_date_buffer), "%a %Y-%m-%d DOY %j", tick_time);
  snprintf(s_mjd_buffer, 12, "MJD %d  ", (int)mjd_time);
  snprintf(s_lst_time_buffer, 6, "%02d:%02d", lst_hour, lst_min);
  strftime(s_utc_time_buffer, sizeof(s_utc_time_buffer), "%H:%M", utc_tick_time);
  if (tick_time->tm_isdst) {
    snprintf(s_local_is_dst, 3, "DST");
  } else {
    snprintf(s_local_is_dst, 3, "   ");
  }
  
  // Display the times in the appropriate segments.
  text_layer_set_text(s_local_time, s_local_time_buffer);
  text_layer_set_text(s_local_date, s_local_date_buffer);
  text_layer_set_text(s_local_dst, s_local_is_dst);
  text_layer_set_text(s_utc_time, s_utc_time_buffer);
  text_layer_set_text(s_mjd, s_mjd_buffer);
  text_layer_set_text(s_lst_time, s_lst_time_buffer);
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Calculate the dimensions for each of the face segments.
  float label_width = bounds.size.w / 6.0;
  float local_panel_height = bounds.size.h / 3.0;
  float time_width = bounds.size.w - label_width;
  float dst_height = local_panel_height / 4.0;
  float date_height = local_panel_height / 4.0;
  float dst_sy = local_panel_height - dst_height * 1.2;
  float date_sy = local_panel_height - date_height * 0.1;
  float utc_sy = local_panel_height + date_height * 1.5;
  float utc_panel_height = bounds.size.h / 3.0;
  float mjd_sy = local_panel_height + date_height;
  float mjd_height = date_height;
  float lst_sy = mjd_sy + utc_panel_height;
  float lst_panel_height = bounds.size.h - lst_sy;
  
  // Make the segments with our segment maker.
  // The Local time segment label (which is "L").
#ifdef PBL_SDK_2
  int GColorYellow, GColorPastelYellow, GColorMintGreen, GColorPictonBlue;
  GColorYellow=GColorWhite;
  GColorPastelYellow=GColorMintGreen=GColorPictonBlue=GColorClear;
#endif
  add_window_element(window_layer, &s_local_label, GColorBlack, GColorWhite,
                     fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), GTextAlignmentCenter,
                     0, 0, label_width, local_panel_height);
  text_layer_set_text(s_local_label, "L");

  // The Local time segment indicator for daylight savings time.
  add_window_element(window_layer, &s_local_dst, GColorBlack, GColorYellow,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GTextAlignmentCenter,
                     0, dst_sy, label_width, dst_height);

  // The Local time segment.
  add_window_element(window_layer, &s_local_time, GColorPastelYellow, GColorBlack,
                     fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS), GTextAlignmentCenter,
                     label_width, 0, time_width, local_panel_height);

  // The Local date segment.
  add_window_element(window_layer, &s_local_date, GColorClear, GColorBlack,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GTextAlignmentCenter,
                     0, date_sy, bounds.size.w, date_height);

  // The UTC time segment label (which is "U").
  add_window_element(window_layer, &s_utc_label, GColorBlack, GColorWhite,
                     fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), GTextAlignmentCenter,
                     0, mjd_sy, label_width, utc_panel_height);
  text_layer_set_text(s_utc_label, "U");

  // The UTC time segment.
  add_window_element(window_layer, &s_utc_time, GColorMintGreen, GColorBlack,
                     fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS), GTextAlignmentCenter,
                     label_width, utc_sy, time_width, utc_panel_height);

  // The MJD time segment.
  add_window_element(window_layer, &s_mjd, GColorMintGreen, GColorBlack,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GTextAlignmentRight,
                     label_width, mjd_sy, time_width, mjd_height);

  // The LST time segment label (which is "S").
  add_window_element(window_layer, &s_lst_label, GColorBlack, GColorWhite,
                     fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), GTextAlignmentCenter,
                     0, lst_sy, label_width, lst_panel_height);
  text_layer_set_text(s_lst_label, "S");

  // The LST time segment.
  add_window_element(window_layer, &s_lst_time, GColorPictonBlue, GColorBlack,
                     fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS), GTextAlignmentCenter,
                     label_width, lst_sy, time_width, lst_panel_height);
}

static void main_window_unload(Window *window) {
  // Destroy all the text layers that we made.
  int i;
  for (i = 0; i < num_text_layers; i++) {
    text_layer_destroy(all_text_layers[i]);
  }
  
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  // Get user-set configuration. 
  Tuple *longitude_t = dict_find(iter, KEY_LONGITUDE);
  if (longitude_t) {
    double lng = (double)longitude_t->value->int32 / 1e7;
    persist_write_data(KEY_LONGITUDE, &lng, sizeof(double));
    // Do an immediate update of the time.
    update_time();
  }
}

static void handle_init(void) {
  s_my_window = window_create();

  window_set_window_handlers(s_my_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  
  window_stack_push(s_my_window, true);
  
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void handle_deinit(void) {
  window_destroy(s_my_window);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Run three times per minute.
  if (tick_time->tm_sec % 20) {
    return;
  }
  update_time();
}

int main(void) {
  handle_init();
  // Register with TickTimerService
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  update_time();
  app_event_loop();
  tick_timer_service_unsubscribe();
  handle_deinit();
}
