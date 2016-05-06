#include <pebble.h>

#define KEY_LONGITUDE 0

static Window *s_my_window;
static TextLayer *s_local_time, *s_local_label, *s_local_dst, *s_local_date;
static TextLayer *s_utc_label, *s_utc_time, *s_mjd;
static TextLayer *s_lst_label, *s_lst_time;

int num_text_layers = 0;
TextLayer **all_text_layers = NULL;

// Structure containing all the required values for a particular element.
typedef struct _element_position {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
} elementPosition;

typedef struct _element_properties {
  Layer *window_layer;
  TextLayer **text_layer;
  GColor background_colour;
  GColor foreground_colour;
  GFont text_font;
  GTextAlignment text_alignment;
  elementPosition element_position;
} elementProperties;

// An all-in-one section maker.
static void add_window_element(elementProperties *element_properties) {
  *(element_properties->text_layer) = text_layer_create(GRect(element_properties->element_position.x, 
                                                      element_properties->element_position.y, 
                                                      element_properties->element_position.w, 
                                                      element_properties->element_position.h));
  text_layer_set_background_color(*(element_properties->text_layer), element_properties->background_colour);
  text_layer_set_text_color(*(element_properties->text_layer), element_properties->foreground_colour);
  text_layer_set_font(*(element_properties->text_layer), element_properties->text_font);
  text_layer_set_text_alignment(*(element_properties->text_layer), element_properties->text_alignment);
  layer_add_child(element_properties->window_layer, text_layer_get_layer(*(element_properties->text_layer)));

  // We keep track of all our text layers so we can destroy them when we get unloaded.
  num_text_layers++;
  all_text_layers = realloc(all_text_layers, num_text_layers * sizeof(TextLayer *));
  all_text_layers[num_text_layers - 1] = *(element_properties->text_layer);
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
  text_layer_set_text(s_utc_time, s_utc_time_buffer);
  text_layer_set_text(s_local_time, s_local_time_buffer);
  text_layer_set_text(s_local_date, s_local_date_buffer);
  text_layer_set_text(s_mjd, s_mjd_buffer);
  text_layer_set_text(s_lst_time, s_lst_time_buffer);
  text_layer_set_text(s_local_dst, s_local_is_dst);
}

static void add_element_to_draw(elementProperties ***eorder, int *nprops,
                                elementProperties *element) {
  *eorder = (elementProperties **)realloc(*eorder, (*nprops + 1) * sizeof(elementProperties *));
  (*eorder)[*nprops] = element;
  (*nprops)++;
}

static void main_window_load(Window *window) {
  int i;
  // Get information about the Window.
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // All the elements we want to make.
  elementProperties ep_local_time, ep_utc_time, ep_lst_time;
  elementProperties ep_local_label, ep_utc_label, ep_lst_label;
  elementProperties ep_date, ep_mjd, ep_dst;
  // The order that we will draw the elements.
  elementProperties **elementOrder = NULL;
  int nelements = 0;
  
  // Set the dimensions for each of the face segments.
  
  // First, the properties that won't change between rectangular and round faces.
  // Associate the correct label pointers.
  ep_local_label.text_layer = &s_local_label;
  ep_utc_label.text_layer = &s_utc_label;
  ep_lst_label.text_layer = &s_lst_label;
  ep_local_time.text_layer = &s_local_time;
  ep_utc_time.text_layer = &s_utc_time;
  ep_lst_time.text_layer = &s_lst_time;
  ep_date.text_layer = &s_local_date;
  ep_mjd.text_layer = &s_mjd;
  ep_dst.text_layer = &s_local_dst;
  // The foreground and background colours of each of the time panels is set for
  // a reason.
  // Local time is solar time, therefore yellow like the Sun.
  ep_local_time.background_colour = GColorPastelYellow;
  // UTC is time at the Greenwich observatory, therefore green.
  ep_utc_time.background_colour = GColorMintGreen;
  // LST is sky time, therefore blue like the sky.
  ep_lst_time.background_colour = GColorPictonBlue;
  // All times are in black for easy reading.
  ep_local_time.foreground_colour = GColorBlack;
  ep_utc_time.foreground_colour = GColorBlack;
  ep_lst_time.foreground_colour = GColorBlack;
  // And the times are always centered.
  ep_local_time.text_alignment = GTextAlignmentCenter;
  ep_utc_time.text_alignment = GTextAlignmentCenter;
  ep_lst_time.text_alignment = GTextAlignmentCenter;
  // The date looks the same in both faces too.
  ep_date.background_colour = GColorWhite;
  ep_date.foreground_colour = GColorBlack;
  ep_date.text_alignment = GTextAlignmentCenter;
  // The MJD is associated with UTC, so it gets the same colours.
  ep_mjd.background_colour = ep_utc_time.background_colour;
  ep_mjd.foreground_colour = ep_utc_time.foreground_colour;

  // Define some heights.
  int16_t full_height = bounds.size.h / 3;
  int16_t small_height = full_height / 4;
  
#ifdef PBL_PLATFORM_BASALT
  int16_t medium_height = bounds.size.h - 2 * full_height - small_height;
  // We're running on a Pebble Time.
  // The rectangular watches get three rectangular panels, and
  // their labels (L, U, S) go on the left in reverse text.
  
  // Begin with each of the labels, all of which go to the left.
  ep_local_label.element_position.x = 0;
  ep_utc_label.element_position.x = 0;
  ep_lst_label.element_position.x = 0;
  // Each have the same width.
  int16_t label_width = bounds.size.w / 6;
  ep_local_label.element_position.w = label_width;
  ep_utc_label.element_position.w = label_width;
  ep_lst_label.element_position.w = label_width;
  // Set their heights.
  ep_local_label.element_position.h = full_height;
  ep_utc_label.element_position.h = full_height;
  ep_lst_label.element_position.h = medium_height;
  // Calculate their starting y locations.
  ep_local_label.element_position.y = 0;
  ep_utc_label.element_position.y = ep_local_label.element_position.y + full_height +
    small_height;
  ep_lst_label.element_position.y = bounds.size.h - ep_lst_label.element_position.h;
  // Each label has the same foreground and background colour.
  ep_local_label.background_colour = GColorBlack;
  ep_utc_label.background_colour = GColorBlack;
  ep_lst_label.background_colour = GColorBlack;
  ep_local_label.foreground_colour = GColorWhite;
  ep_utc_label.foreground_colour = GColorWhite;
  ep_lst_label.foreground_colour = GColorWhite;
  // And each uses the same font and alignment.
  ep_local_label.text_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  ep_utc_label.text_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  ep_lst_label.text_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  ep_local_label.text_alignment = GTextAlignmentCenter;
  ep_utc_label.text_alignment = GTextAlignmentCenter;
  ep_lst_label.text_alignment = GTextAlignmentCenter;
    
  // Add the labels to the draw order.
  add_element_to_draw(&elementOrder, &nelements, &ep_local_label);
  add_element_to_draw(&elementOrder, &nelements, &ep_utc_label);
  add_element_to_draw(&elementOrder, &nelements, &ep_lst_label);

  // Now, do the time elements, which all appear on the right.
  ep_local_time.element_position.x = label_width;
  ep_utc_time.element_position.x = label_width;
  ep_lst_time.element_position.x = label_width;
  // They all have the same width.
  int16_t time_width = bounds.size.w - label_width;
  ep_local_time.element_position.w = time_width;
  ep_utc_time.element_position.w = time_width;
  ep_lst_time.element_position.w = time_width;
  // They all start at the same height and have the same height as their
  // associated labels.
  ep_local_time.element_position.y = ep_local_label.element_position.y;
  ep_utc_time.element_position.y = ep_utc_label.element_position.y + small_height * 0.5;
  ep_lst_time.element_position.y = ep_lst_label.element_position.y;
  ep_local_time.element_position.h = ep_local_label.element_position.h;
  ep_utc_time.element_position.h = ep_utc_label.element_position.h;
  ep_lst_time.element_position.h = ep_lst_label.element_position.h;
  // The font used depends on the height of the panel.
  ep_local_time.text_font = fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS);
  ep_utc_time.text_font = fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS);
  ep_lst_time.text_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);

  // Now the two date elements.
  // The local date appears just below the local time, and covers the
  // entire width of the display.
  ep_date.element_position.x = 0;
  ep_date.element_position.w = bounds.size.w;
  ep_date.element_position.h = small_height;
  ep_date.element_position.y = ep_local_time.element_position.y + ep_local_time.element_position.h -
    ep_date.element_position.h * 0.1;
  // It uses a smallish font.
  ep_date.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  // The MJD is just above the UTC.
  ep_mjd.element_position.x = label_width;
  ep_mjd.element_position.w = time_width;
  ep_mjd.element_position.h = small_height;
  ep_mjd.element_position.y = ep_local_time.element_position.y + ep_local_time.element_position.h +
    ep_date.element_position.h;
  // And its text is aligned to the right.
  ep_mjd.text_alignment = GTextAlignmentRight;
  ep_mjd.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  // The DST indicator is under the "L" label (because DST is local).
  ep_dst.element_position.x = 0;
  ep_dst.element_position.w = label_width;
  ep_dst.element_position.h = small_height;
  ep_dst.element_position.y = ep_local_label.element_position.h - ep_dst.element_position.h * 1.2;
  // It has label colouring, except yellow text.
  ep_dst.background_colour = GColorBlack;
  ep_dst.foreground_colour = GColorYellow;
  // Small text, centered.
  ep_dst.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  ep_dst.text_alignment = GTextAlignmentCenter;
  
  // Add the panels in the right order.
  add_element_to_draw(&elementOrder, &nelements, &ep_local_time);
  add_element_to_draw(&elementOrder, &nelements, &ep_date);
  add_element_to_draw(&elementOrder, &nelements, &ep_utc_time);
  add_element_to_draw(&elementOrder, &nelements, &ep_lst_time);
  add_element_to_draw(&elementOrder, &nelements, &ep_mjd);
  add_element_to_draw(&elementOrder, &nelements, &ep_dst);
#endif
#ifdef PBL_PLATFORM_CHALK
  // We're running on a Pebble Time Round.
  // Round watches have a slightly different layout, but the
  // same three panels.

  // The panels are mixed together somewhat more than for the rectangular face,
  // so we define things a bit differently here.
  // All elements now take up the entire width of the face, and are centered.
  ep_local_label.element_position.x = 0;
  ep_utc_label.element_position.x = 0;
  ep_lst_label.element_position.x = 0;
  ep_local_time.element_position.x = 0;
  ep_utc_time.element_position.x = 0;
  ep_lst_time.element_position.x = 0;
  ep_date.element_position.x = 0;
  ep_mjd.element_position.x = 0;
  ep_local_label.element_position.w = bounds.size.w;
  ep_utc_label.element_position.w = bounds.size.w;
  ep_lst_label.element_position.w = bounds.size.w;
  ep_local_time.element_position.w = bounds.size.w;
  ep_utc_time.element_position.w = bounds.size.w;
  ep_lst_time.element_position.w = bounds.size.w;
  ep_date.element_position.w = bounds.size.w;
  ep_mjd.element_position.w = bounds.size.w;
  ep_local_label.text_alignment = GTextAlignmentCenter;
  ep_utc_label.text_alignment = GTextAlignmentCenter;
  ep_lst_label.text_alignment = GTextAlignmentCenter;
  ep_mjd.text_alignment = GTextAlignmentCenter;

  // Determine the positions of the labels and the panels together.
  // At the top is the UTC label, and below that the time and MJD.
  ep_utc_label.element_position.h = small_height;
  ep_utc_label.element_position.y = 0;
  ep_utc_label.background_colour = GColorBlack;
  ep_utc_label.foreground_colour = GColorWhite;
  ep_utc_time.element_position.h = full_height;
  ep_utc_time.element_position.y = ep_utc_label.element_position.y + ep_utc_label.element_position.h * 0.5;
  ep_mjd.element_position.h = small_height;
  ep_mjd.element_position.y = ep_utc_time.element_position.h - ep_mjd.element_position.h * 0.65;
  ep_mjd.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  // Followed now by the local date.
  ep_date.element_position.h = small_height;
  ep_date.element_position.y = ep_utc_time.element_position.h + ep_date.element_position.h * 1.2;
  ep_date.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  
  // Then the local label and the time (this is where the watch is widest).
  ep_local_label.element_position.h = small_height;
  ep_local_label.element_position.y = ep_utc_time.element_position.h + ep_date.element_position.h * 0.35;
  ep_local_time.element_position.h = full_height - ep_date.element_position.h;
  ep_local_time.element_position.y = ep_date.element_position.y + ep_date.element_position.h * 0.5;

  // The DST indicator is to the right of the local time (because DST is local).
  ep_dst.element_position.x = bounds.size.w * 0.84;
  ep_dst.element_position.w = bounds.size.w - ep_dst.element_position.x;
  ep_dst.element_position.h = small_height;
  ep_dst.element_position.y = ep_local_time.element_position.y + 1.5 * ep_dst.element_position.h;
  // It has the same colour as the local time, except red text.
  ep_dst.background_colour = ep_local_time.background_colour;
  ep_dst.foreground_colour = GColorBlack;
  // Small text, centered.
  ep_dst.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  ep_dst.text_alignment = GTextAlignmentLeft;
  
  // Finally, the LST time and then label.
  ep_lst_time.element_position.y = ep_local_time.element_position.y + ep_local_time.element_position.h * 0.9;
  ep_lst_time.element_position.h = bounds.size.h - ep_lst_time.element_position.y;
  ep_lst_label.element_position.h = small_height;
  ep_lst_label.element_position.y = bounds.size.h - ep_lst_label.element_position.h;
  
  // Each label has the same foreground and background colour.
  ep_local_label.background_colour = GColorBlack;
  ep_lst_label.background_colour = GColorBlack;
  ep_local_label.foreground_colour = GColorWhite;
  ep_lst_label.foreground_colour = GColorWhite;
  // And each uses the same font and alignment.
  ep_local_label.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  ep_utc_label.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  ep_lst_label.text_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  // The font used for each time depends on the height of the panel.
  ep_local_time.text_font = fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS);
  ep_utc_time.text_font = fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS);
  ep_lst_time.text_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);

  // Add the panels in the right order.
  add_element_to_draw(&elementOrder, &nelements, &ep_utc_time);
  add_element_to_draw(&elementOrder, &nelements, &ep_utc_label);
  add_element_to_draw(&elementOrder, &nelements, &ep_lst_time);
  add_element_to_draw(&elementOrder, &nelements, &ep_local_time);
  add_element_to_draw(&elementOrder, &nelements, &ep_dst);
  add_element_to_draw(&elementOrder, &nelements, &ep_mjd);
  add_element_to_draw(&elementOrder, &nelements, &ep_date);
  add_element_to_draw(&elementOrder, &nelements, &ep_local_label);
  add_element_to_draw(&elementOrder, &nelements, &ep_lst_label);
#endif
  
#ifdef PBL_SDK_2
  int GColorYellow, GColorPastelYellow, GColorMintGreen, GColorPictonBlue;
  GColorYellow=GColorWhite;
  GColorPastelYellow=GColorMintGreen=GColorPictonBlue=GColorClear;
#endif

  // Go through the labels to make in the right order.
  for (i = 0; i < nelements; i++) {
    elementOrder[i]->window_layer = window_layer;
    *(elementOrder[i]->text_layer) = NULL;
    add_window_element(elementOrder[i]);
  }

// Set the text for each label.

#ifdef PBL_PLATFORM_BASALT
  text_layer_set_text(s_utc_label, "U");
  text_layer_set_text(s_local_label, "L");
  text_layer_set_text(s_lst_label, "S");
#endif
#ifdef PBL_PLATFORM_CHALK
  text_layer_set_text(s_utc_label, "UTC");
  text_layer_set_text(s_local_label, "LOCAL");
  text_layer_set_text(s_lst_label, "LST");
#endif
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
