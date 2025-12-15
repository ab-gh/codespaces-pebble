#include <pebble.h>
#include "num2words.h"

enum WeatherKey {
  WEATHER_ICON_KEY = 0x0,
  WEATHER_TEMPERATURE_KEY = 0x1,
  WEATHER_CITY_KEY = 0x2,
};

typedef enum {
  MOVING_IN,
  IN_FRAME,
  PREPARE_TO_MOVE,
  MOVING_OUT
} SlideState;

typedef struct {
  TextLayer *label;
  SlideState state; // animation state
  char *next_string; // what to say in the next phase of animation
  bool unchanged_font;

  int left_pos;
  int right_pos;
  int still_pos;

  int movement_delay;
  int delay_count;
} SlidingRow;

typedef struct {
  TextLayer *demo_label;
  SlidingRow rows[4];
  SlidingRow date_row;  // separate row for the date part
  SlidingRow battery_row;  // battery percentage row
  SlidingRow weather_row;  // weather temperature row
  SlidingRow weather_condition_row;  // weather condition row
  SlidingRow steps_row;  // step count row
  int last_hour;
  int last_minute;
  int last_day;
  int last_battery;
  int last_temperature;
  int last_steps;

  GFont bitham42_bold;
  GFont bitham42_light;
  GFont gothic18_bold;
  GFont gothic18;

  Window *window;
  Animation *animation;

  struct SlidingTextRenderState {
    // double buffered string storage
    char hours[2][32];
    uint8_t next_hours;
    char first_minutes[2][32];
    char second_minutes[2][32];
    uint8_t next_minutes;
    char days[2][32];
    uint8_t next_days;
    char dates[2][32];
    uint8_t next_dates;
    char battery[2][64];
    uint8_t next_battery;
    char temperature[2][32];
    uint8_t next_temperature;
    char weather_condition[2][32];
    uint8_t next_weather_condition;
    char steps[2][32];
    uint8_t next_steps;

    struct SlidingTextRenderDemoTime {
      int secs;
      int mins;
      int hour;
    } demo_time;

  } render_state;

} SlidingTextData;

SlidingTextData *s_data;

static void day_to_word(int day, char *buffer) {
  const char *days[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
  strcpy(buffer, days[day]);
}

static void date_to_words(int day, int month, char *buffer) {
  const char *months[] = {"january", "february", "march", "april", "may", "june",
                          "july", "august", "september", "october", "november", "december"};
  
  const char *suffix = "th";
  if (day == 1 || day == 21 || day == 31) suffix = "st";
  else if (day == 2 || day == 22) suffix = "nd";
  else if (day == 3 || day == 23) suffix = "rd";
  
  snprintf(buffer, 32, "%d%s %s", day, suffix, months[month]);
}

static void number_to_words(int num, char *buffer) {
  const char *ones[] = {"", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
  const char *teens[] = {"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", 
                         "sixteen", "seventeen", "eighteen", "nineteen"};
  const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
  
  if (num == 0) {
    strcpy(buffer, "zero");
  } else if (num == 100) {
    strcpy(buffer, "one hundred");
  } else if (num < 10) {
    strcpy(buffer, ones[num]);
  } else if (num < 20) {
    strcpy(buffer, teens[num - 10]);
  } else if (num < 100) {
    int ten = num / 10;
    int one = num % 10;
    if (one == 0) {
      strcpy(buffer, tens[ten]);
    } else {
      snprintf(buffer, 64, "%s %s", tens[ten], ones[one]);
    }
  }
}

static void init_sliding_row(SlidingTextData *data, SlidingRow *row, GRect pos, GFont font,
        int delay) {
  row->label = text_layer_create(pos);
  text_layer_set_text_alignment(row->label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  text_layer_set_background_color(row->label, GColorClear);
  text_layer_set_text_color(row->label, GColorWhite);
  if (font) {
    text_layer_set_font(row->label, font);
    row->unchanged_font = true;
  } else {
    row->unchanged_font = false;
  }

  row->state = IN_FRAME;
  row->next_string = NULL;

  row->left_pos = -pos.size.w;
  row->right_pos = pos.size.w;
  row->still_pos = pos.origin.x;

  row->movement_delay = delay;
  row->delay_count = 0;

  data->last_hour = -1;
  data->last_minute = -1;
  data->last_day = -1;
  data->last_battery = -1;
  data->last_temperature = 999;
  data->last_steps = -1;
}

static void slide_in_text(SlidingTextData *data, SlidingRow *row, char* new_text) {
  (void) data;

  const char *old_text = text_layer_get_text(row->label);
  if (old_text) {
    row->next_string = new_text;
    row->state = PREPARE_TO_MOVE;
  } else {
    text_layer_set_text(row->label, new_text);
    GRect frame = layer_get_frame(text_layer_get_layer(row->label));
    frame.origin.x = row->right_pos;
    layer_set_frame(text_layer_get_layer(row->label), frame);
    row->state = MOVING_IN;
  }
}


static bool update_sliding_row(SlidingTextData *data, SlidingRow *row) {
  (void) data;

  GRect frame = layer_get_frame(text_layer_get_layer(row->label));
  bool something_changed = true;
  switch (row->state) {
    case PREPARE_TO_MOVE:
      frame.origin.x = row->still_pos;
      row->delay_count++;
      if (row->delay_count > row->movement_delay) {
        row->state = MOVING_OUT;
        row->delay_count = 0;
      }
    break;

    case MOVING_IN: {
      int speed = abs(frame.origin.x - row->still_pos) / 3 + 1;
      frame.origin.x -= speed;
      if (frame.origin.x <= row->still_pos) {
        frame.origin.x = row->still_pos;
        row->state = IN_FRAME;
      }
    }
    break;

    case MOVING_OUT: {
      int speed = abs(frame.origin.x - row->still_pos) / 3 + 1;
      frame.origin.x -= speed;

      if (frame.origin.x <= row->left_pos) {
        frame.origin.x = row->right_pos;
        row->state = MOVING_IN;
        text_layer_set_text(row->label, row->next_string);
        row->next_string = NULL;
      }
    }
    break;

    case IN_FRAME:
    default:
      something_changed = false;
      break;
  }
  if (something_changed) {
    layer_set_frame(text_layer_get_layer(row->label), frame);
  }
  return something_changed;
}

static void animation_update(struct Animation *animation, const AnimationProgress time_normalized) {
  SlidingTextData *data = s_data;

  struct SlidingTextRenderState *rs = &data->render_state;

  time_t now = time(NULL);
  struct tm t = *localtime(&now);

  bool something_changed = false;

  if (data->last_day != t.tm_wday) {
    something_changed = true;
    // Update day of the week and date
    day_to_word(t.tm_wday, rs->days[rs->next_days]);
    date_to_words(t.tm_mday, t.tm_mon, rs->dates[rs->next_dates]);
    slide_in_text(data, &data->rows[0], rs->days[rs->next_days]);
    slide_in_text(data, &data->date_row, rs->dates[rs->next_dates]);
    rs->next_days = rs->next_days ? 0 : 1;
    rs->next_dates = rs->next_dates ? 0 : 1;
    data->last_day = t.tm_wday;
  }

  if (data->last_minute != t.tm_min) {
    something_changed = true;

    minute_to_formal_words(t.tm_min, rs->first_minutes[rs->next_minutes], rs->second_minutes[rs->next_minutes]);
    
    // For minutes 1-9, minute_to_formal_words puts the word in first_word
    // We need to move it to second_word and put "oh" in first_word
    if (t.tm_min > 0 && t.tm_min < 10) {
      strcpy(rs->second_minutes[rs->next_minutes], rs->first_minutes[rs->next_minutes]);
      strcpy(rs->first_minutes[rs->next_minutes], "oh");
    }
    
    if(data->last_hour != t.tm_hour || t.tm_min <= 20
       || t.tm_min/10 != data->last_minute/10) {
      slide_in_text(data, &data->rows[2], rs->first_minutes[rs->next_minutes]);
    } else {
      // The tens line didn't change, so swap to the correct buffer but don't animate
      text_layer_set_text(data->rows[2].label, rs->first_minutes[rs->next_minutes]);
    }
    slide_in_text(data, &data->rows[3], rs->second_minutes[rs->next_minutes]);
    rs->next_minutes = rs->next_minutes ? 0 : 1;
    data->last_minute = t.tm_min;
  }

  if (data->last_hour != t.tm_hour) {
    hour_to_12h_word(t.tm_hour, rs->hours[rs->next_hours]);
    slide_in_text(data, &data->rows[1], rs->hours[rs->next_hours]);
    rs->next_hours = rs->next_hours ? 0 : 1;
    data->last_hour = t.tm_hour;
  }

  for (size_t i = 0; i < ARRAY_LENGTH(data->rows); ++i) {
    something_changed = update_sliding_row(data, &data->rows[i]) || something_changed;
  }
  something_changed = update_sliding_row(data, &data->date_row) || something_changed;
  something_changed = update_sliding_row(data, &data->battery_row) || something_changed;
  something_changed = update_sliding_row(data, &data->weather_row) || something_changed;
  something_changed = update_sliding_row(data, &data->weather_condition_row) || something_changed;
  something_changed = update_sliding_row(data, &data->steps_row) || something_changed;

  if (!something_changed) {
    animation_unschedule(data->animation);
  }
}

static void make_animation() {
  s_data->animation = animation_create();
  animation_set_duration(s_data->animation, ANIMATION_DURATION_INFINITE);
                  // the animation will stop itself
  static const struct AnimationImplementation s_animation_implementation = {
    .update = animation_update,
  };
  animation_set_implementation(s_data->animation, &s_animation_implementation);
  animation_schedule(s_data->animation);
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  make_animation();
}

static void handle_battery(BatteryChargeState charge_state) {
  SlidingTextData *data = s_data;
  struct SlidingTextRenderState *rs = &data->render_state;
  
  int battery_percent = charge_state.charge_percent;
  
  if (data->last_battery != battery_percent) {
    char num_words[64];
    number_to_words(battery_percent, num_words);
    snprintf(rs->battery[rs->next_battery], 64, "%s %%", num_words);
    slide_in_text(data, &data->battery_row, rs->battery[rs->next_battery]);
    rs->next_battery = rs->next_battery ? 0 : 1;
    data->last_battery = battery_percent;
    make_animation();
  }
}

static void health_handler(HealthEventType event, void *context) {
  SlidingTextData *data = s_data;
  struct SlidingTextRenderState *rs = &data->render_state;
  
  if (event == HealthEventMovementUpdate) {
    HealthMetric metric = HealthMetricStepCount;
    time_t start = time_start_of_today();
    time_t end = time(NULL);
    
    HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
    
    if (mask & HealthServiceAccessibilityMaskAvailable) {
      int steps = (int)health_service_sum_today(metric);
      
      if (data->last_steps != steps) {
        snprintf(rs->steps[rs->next_steps], 32, "%d", steps);
        slide_in_text(data, &data->steps_row, rs->steps[rs->next_steps]);
        rs->next_steps = rs->next_steps ? 0 : 1;
        data->last_steps = steps;
        make_animation();
      }
    }
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  SlidingTextData *data = s_data;
  struct SlidingTextRenderState *rs = &data->render_state;
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Received message from phone");
  
  Tuple *temp_tuple = dict_find(iterator, WEATHER_TEMPERATURE_KEY);
  Tuple *condition_tuple = dict_find(iterator, WEATHER_CITY_KEY);
  
  if (temp_tuple) {
    int temperature = (int)temp_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_INFO, "Temperature: %d", temperature);
    
    if (data->last_temperature != temperature) {
      snprintf(rs->temperature[rs->next_temperature], 32, "%dc", temperature);
      slide_in_text(data, &data->weather_row, rs->temperature[rs->next_temperature]);
      rs->next_temperature = rs->next_temperature ? 0 : 1;
      data->last_temperature = temperature;
      make_animation();
    }
  }
  
  if (condition_tuple) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Condition: %s", condition_tuple->value->cstring);
    strncpy(rs->weather_condition[rs->next_weather_condition], condition_tuple->value->cstring, 31);
    rs->weather_condition[rs->next_weather_condition][31] = '\0';
    slide_in_text(data, &data->weather_condition_row, rs->weather_condition[rs->next_weather_condition]);
    rs->next_weather_condition = rs->next_weather_condition ? 0 : 1;
    make_animation();
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", reason);
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

static void request_weather(void) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  if (!iter) {
    return;
  }

  int value = 1;
  dict_write_int(iter, 1, &value, sizeof(int), true);
  dict_write_end(iter);

  app_message_outbox_send();
}

static void handle_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  #if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
  #endif
  free(s_data);
}

static void handle_init() {
  SlidingTextData *data = (SlidingTextData*)malloc(sizeof(SlidingTextData));
  s_data = data;

  data->render_state.next_hours = 0;
  data->render_state.next_minutes = 0;
  data->render_state.next_days = 0;
  data->render_state.next_dates = 0;
  data->render_state.next_battery = 0;
  data->render_state.next_temperature = 0;
  data->render_state.next_weather_condition = 0;
  data->render_state.next_steps = 0;
  data->render_state.demo_time.secs = 0;
  data->render_state.demo_time.mins = 0;
  data->render_state.demo_time.hour = 0;

  data->window = window_create();

  window_set_background_color(data->window, GColorBlack);

  data->bitham42_bold = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  data->bitham42_light = fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
  data->gothic18_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  data->gothic18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);

  Layer *window_layer = window_get_root_layer(data->window);
  GRect layer_frame = layer_get_frame(window_layer);
  const int16_t width = layer_frame.size.w;
  const int16_t padding = 5;

  init_sliding_row(data, &data->rows[0], GRect(0, 0, width - padding, 60), data->gothic18_bold, 6);
  text_layer_set_text_alignment(data->rows[0].label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(data->rows[0].label));

  init_sliding_row(data, &data->date_row, GRect(0, 15, width - padding, 60), data->gothic18, 6);
  text_layer_set_text_alignment(data->date_row.label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(data->date_row.label));

  init_sliding_row(data, &data->rows[1], GRect(0, 26, width, 60), data->bitham42_bold, 6);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[1].label));

  init_sliding_row(data, &data->rows[2], GRect(0, 62, width, 96), data->bitham42_light, 3);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[2].label));

  init_sliding_row(data, &data->rows[3], GRect(0, 98, width, 132), data->bitham42_light, 0);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[3].label));
  init_sliding_row(data, &data->steps_row, GRect(0, 142, width / 2, 168), data->gothic18_bold, 6);
  text_layer_set_text_alignment(data->steps_row.label, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(data->steps_row.label));
  init_sliding_row(data, &data->battery_row, GRect(0, 142, width - padding, 168), data->gothic18, 6);
  text_layer_set_text_alignment(data->battery_row.label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(data->battery_row.label));

  init_sliding_row(data, &data->weather_condition_row, GRect(0, 0, width / 2, 60), data->gothic18, 6);
  text_layer_set_text_alignment(data->weather_condition_row.label, GTextAlignmentLeft);
  text_layer_set_text(data->weather_condition_row.label, "loading");
  layer_add_child(window_layer, text_layer_get_layer(data->weather_condition_row.label));

  init_sliding_row(data, &data->weather_row, GRect(0, 15, width / 2, 60), data->gothic18_bold, 6);
  text_layer_set_text_alignment(data->weather_row.label, GTextAlignmentLeft);
  text_layer_set_text(data->weather_row.label, "--c");
  layer_add_child(window_layer, text_layer_get_layer(data->weather_row.label));

  GFont norm14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  data->demo_label = text_layer_create(GRect(0, -3, 100, 20));
  text_layer_set_background_color(data->demo_label, GColorClear);
  text_layer_set_text_color(data->demo_label, GColorWhite);
  text_layer_set_font(data->demo_label, norm14);
  text_layer_set_text(data->demo_label, "demo mode");
  layer_add_child(window_layer, text_layer_get_layer(data->demo_label));

  layer_set_hidden(text_layer_get_layer(data->demo_label), true);
  layer_mark_dirty(window_layer);

  make_animation();

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  battery_state_service_subscribe(handle_battery);
  handle_battery(battery_state_service_peek());

  // Subscribe to health events
  #if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
  // Get initial step count
  health_handler(HealthEventMovementUpdate, NULL);
  #endif

  // Setup AppMessage for weather
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(256, 256);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage opened, requesting weather");
  request_weather();

  const bool animated = true;
  window_stack_push(data->window, animated);
}

int main(void) {
  handle_init();

  app_event_loop();

  handle_deinit();
}
