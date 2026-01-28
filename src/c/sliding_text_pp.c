#include <pebble.h>
#include "num2words.h"

// ============================================================================
// ANIMATION CONFIGURATION - Change to ANIMATION_STYLE_SLIDE to revert
// ============================================================================
#define ANIMATION_STYLE_HACKER 1
#define ANIMATION_STYLE_SLIDE 0
#define ANIMATION_STYLE ANIMATION_STYLE_HACKER

#define HACKER_ANIMATION_SPEED_MS 70
#define HACKER_MAX_ITERATIONS 12
#define HACKER_MIN_ITERATIONS 6
#define HACKER_STARTUP_DELAY_MS 50

static void window_appear_handler(Window *window);

enum WeatherKey {
  WEATHER_TEMPERATURE_KEY = 0x1,
  WEATHER_CITY_KEY = 0x2,
};

#define PERSIST_WEATHER_CONDITION 100
#define PERSIST_WEATHER_TEMPERATURE 101

static void request_weather(void);
static void make_animation(void);
static void update_time_display(void);

typedef enum { MOVING_IN, IN_FRAME, PREPARE_TO_MOVE, MOVING_OUT } SlideState;

typedef struct {
  char target_char, current_char;
  int iterations_left;
  bool locked;
} HackerCharState;

typedef struct {
  HackerCharState chars[64];
  int target_length;
  bool animating;
  bool needs_initial_render;
  char target_text[64], display_buffer[64];
} HackerRowState;

typedef struct {
  TextLayer *label;
  SlideState state;
  char *next_string;
  bool unchanged_font;
  int left_pos, right_pos, still_pos, movement_delay, delay_count;
  HackerRowState hacker_state;
} SlidingRow;

typedef struct {
  SlidingRow day_row, hour_row, first_minute_row, second_minute_row, date_row, battery_row, weather_row, weather_condition_row, steps_row;
  int last_hour, last_minute, last_day, last_battery, last_temperature, last_steps, last_step_update_minute;
  bool weather_changed;
  bool window_ready;
  GFont bitham42_bold, bitham42_light, gothic18_bold, gothic18;
  Window *window;
  AppTimer *hacker_timer;
  struct {
    char hours[2][32], first_minutes[2][32], second_minutes[2][32], days[2][32], dates[2][32];
    char battery[2][64], temperature[2][32], weather_condition[2][32], steps[2][32];
    uint8_t next_hours, next_minutes, next_days, next_dates, next_battery, next_temperature, next_weather_condition, next_steps;
  } render_state;
  // Track collapse state for each line pair
  struct {
    bool day_collapsed;        // Line 1: temperature (left) vs day (right)
    bool date_collapsed;       // Line 2: weather condition (left) vs date (right)
    bool battery_collapsed;    // Line 6: steps (left) vs battery (right)
  } collapse_state;
} SlidingTextData;

SlidingTextData *s_data;

// Forward declarations - needed for collapse/uncollapse system
static void slide_in_text(SlidingTextData *data, SlidingRow *row, char* new_text, bool force_animate);
static void day_to_word(int day, char *buffer);
static void day_of_month_to_words(int day, char *buffer);
static void number_to_words(int num, char *buffer);
static int get_screen_width(SlidingTextData *data);
static bool would_collide_with_font(const char *left_text, const char *right_text, GFont left_font, GFont right_font, int screen_width);
static void day_to_short(int day, char *buffer);
static void date_to_short(int day, char *buffer);
static void battery_to_short(int percent, char *buffer);
static bool check_line1_collision(SlidingTextData *data, const char *temp_text, const char *day_full_text);
static bool check_line2_collision(SlidingTextData *data, const char *cond_text, const char *date_full_text);
static bool check_line6_collision(SlidingTextData *data, const char *steps_text, const char *battery_full_text);
static void collapse_line1_right(SlidingTextData *data, int day);
static void uncollapse_line1_right(SlidingTextData *data, int day);
static void collapse_line2_right(SlidingTextData *data, int date);
static void uncollapse_line2_right(SlidingTextData *data, int date);
static void collapse_line6_right(SlidingTextData *data, int battery_percent);
static void uncollapse_line6_right(SlidingTextData *data, int battery_percent);
static void handle_line1_update(SlidingTextData *data, const char *temp_text, int day);
static void handle_line2_update(SlidingTextData *data, const char *cond_text, int date);
static void handle_line6_update(SlidingTextData *data, const char *steps_text, int battery_percent);

// Check if we're in night mode (midnight to 6am) where animations are disabled to save battery
static bool is_night_mode(void) {
  time_t now = time(NULL);
  struct tm t = *localtime(&now);
  return (t.tm_hour >= 0 && t.tm_hour < 6);
}

static void day_to_word(int day, char *buffer) {
  const char *days[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
  strcpy(buffer, days[day]);
}

static bool would_collide_with_font(const char *left_text, const char *right_text, GFont left_font, GFont right_font, int screen_width) {
  if (!left_text || !right_text || strlen(left_text) == 0 || strlen(right_text) == 0) return false;
  
  // Measure actual text widths
  GSize left_size = graphics_text_layout_get_content_size(left_text, left_font, GRect(0, 0, screen_width, 100), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  GSize right_size = graphics_text_layout_get_content_size(right_text, right_font, GRect(0, 0, screen_width, 100), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  
  // Conservative collision detection:
  // Left text: starts at x=2, width = left_size.w
  // Right text: right-aligned, ends at x=(screen_width-5), width = right_size.w
  // 
  // Right text starts at x = (screen_width - 5 - right_size.w)
  // Left text ends at x = (2 + left_size.w)
  // Collision if: (2 + left_size.w) + gap > (screen_width - 5 - right_size.w)
  // 
  // Only consider it a collision if they would actually overlap (very tight)
  int min_gap = 8;  // Very minimal gap - only collapse if truly overlapping
  return (left_size.w + right_size.w + min_gap) > (screen_width - 2);
}

static int get_screen_width(SlidingTextData *data) {
  return layer_get_bounds(window_get_root_layer(data->window)).size.w;
}

// ============================================================================
// COLLISION DETECTION & COLLAPSE/UNCOLLAPSE SYSTEM
// ============================================================================

// Get the SHORT form of a day name (e.g., "wednesday" -> "wed")
static void day_to_short(int day, char *buffer) {
  const char *days_short[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
  strcpy(buffer, days_short[day]);
}

// Get the SHORT form of a date (e.g., 28 -> "28th")
static void date_to_short(int day, char *buffer) {
  const char *suffix = "th";
  if (day == 1 || day == 21 || day == 31) suffix = "st";
  else if (day == 2 || day == 22) suffix = "nd";
  else if (day == 3 || day == 23) suffix = "rd";
  snprintf(buffer, 32, "%d%s", day, suffix);
}

// Get the SHORT form of battery (e.g., "forty five pc" -> "45%")
static void battery_to_short(int percent, char *buffer) {
  snprintf(buffer, 64, "%d%%", percent);
}

// Check if line 1 (temperature left, day right) would collide
// Returns true if collision detected
static bool check_line1_collision(SlidingTextData *data, const char *temp_text, const char *day_full_text) {
  if (!data->window_ready || !temp_text || !day_full_text) return false;
  int screen_width = get_screen_width(data);
  return would_collide_with_font(temp_text, day_full_text, data->gothic18_bold, data->gothic18_bold, screen_width);
}

// Check if line 2 (weather condition left, date right) would collide
// Returns true if collision detected
static bool check_line2_collision(SlidingTextData *data, const char *cond_text, const char *date_full_text) {
  if (!data->window_ready || !cond_text || !date_full_text) return false;
  int screen_width = get_screen_width(data);
  return would_collide_with_font(cond_text, date_full_text, data->gothic18, data->gothic18, screen_width);
}

// Check if line 6 (steps left, battery right) would collide
// Returns true if collision detected
static bool check_line6_collision(SlidingTextData *data, const char *steps_text, const char *battery_full_text) {
  if (!data->window_ready || !steps_text || !battery_full_text) return false;
  int screen_width = get_screen_width(data);
  return would_collide_with_font(steps_text, battery_full_text, data->gothic18_bold, data->gothic18, screen_width);
}

// Collapse line 1 right text (day name to abbreviation)
static void collapse_line1_right(SlidingTextData *data, int day) {
  char short_day[32];
  day_to_short(day, short_day);
  strcpy(data->render_state.days[data->render_state.next_days], short_day);
  slide_in_text(data, &data->day_row, short_day, false);
  data->render_state.next_days = data->render_state.next_days ? 0 : 1;
  data->collapse_state.day_collapsed = true;
}

// Uncollapse line 1 right text (abbreviated day to full name)
static void uncollapse_line1_right(SlidingTextData *data, int day) {
  char full_day[32];
  day_to_word(day, full_day);
  strcpy(data->render_state.days[data->render_state.next_days], full_day);
  slide_in_text(data, &data->day_row, full_day, false);
  data->render_state.next_days = data->render_state.next_days ? 0 : 1;
  data->collapse_state.day_collapsed = false;
}

// Collapse line 2 right text (full date to numeric)
static void collapse_line2_right(SlidingTextData *data, int date) {
  char short_date[32];
  date_to_short(date, short_date);
  strcpy(data->render_state.dates[data->render_state.next_dates], short_date);
  slide_in_text(data, &data->date_row, short_date, false);
  data->render_state.next_dates = data->render_state.next_dates ? 0 : 1;
  data->collapse_state.date_collapsed = true;
}

// Uncollapse line 2 right text (numeric date to full name)
static void uncollapse_line2_right(SlidingTextData *data, int date) {
  char full_date[32];
  day_of_month_to_words(date, full_date);
  strcpy(data->render_state.dates[data->render_state.next_dates], full_date);
  slide_in_text(data, &data->date_row, full_date, false);
  data->render_state.next_dates = data->render_state.next_dates ? 0 : 1;
  data->collapse_state.date_collapsed = false;
}

// Collapse line 6 right text (battery words to percentage)
static void collapse_line6_right(SlidingTextData *data, int battery_percent) {
  char short_battery[64];
  battery_to_short(battery_percent, short_battery);
  strcpy(data->render_state.battery[data->render_state.next_battery], short_battery);
  slide_in_text(data, &data->battery_row, short_battery, false);
  data->render_state.next_battery = data->render_state.next_battery ? 0 : 1;
  data->collapse_state.battery_collapsed = true;
}

// Uncollapse line 6 right text (percentage to battery words)
static void uncollapse_line6_right(SlidingTextData *data, int battery_percent) {
  char full_battery[64];
  char num_words[64];
  number_to_words(battery_percent, num_words);
  snprintf(full_battery, 64, "%s pc", num_words);
  strcpy(data->render_state.battery[data->render_state.next_battery], full_battery);
  slide_in_text(data, &data->battery_row, full_battery, false);
  data->render_state.next_battery = data->render_state.next_battery ? 0 : 1;
  data->collapse_state.battery_collapsed = false;
}

// Handle line 1 collision: evaluate if collapse/uncollapse is needed
// Called whenever temperature or day updates
static void handle_line1_update(SlidingTextData *data, const char *temp_text, int day) {
  time_t now = time(NULL);
  struct tm t = *localtime(&now);
  
  char full_day[32];
  day_to_word(t.tm_wday, full_day);
  
  bool collision = check_line1_collision(data, temp_text, full_day);
  
  if (collision && !data->collapse_state.day_collapsed) {
    // Collision detected and not collapsed -> collapse it
    collapse_line1_right(data, day);
  } else if (!collision && data->collapse_state.day_collapsed) {
    // No collision detected and is collapsed -> try to uncollapse it
    uncollapse_line1_right(data, day);
  }
}

// Handle line 2 collision: evaluate if collapse/uncollapse is needed
// Called whenever weather condition or date updates
static void handle_line2_update(SlidingTextData *data, const char *cond_text, int date) {
  time_t now = time(NULL);
  struct tm t = *localtime(&now);
  
  char full_date[32];
  day_of_month_to_words(t.tm_mday, full_date);
  
  bool collision = check_line2_collision(data, cond_text, full_date);
  
  if (collision && !data->collapse_state.date_collapsed) {
    // Collision detected and not collapsed -> collapse it
    collapse_line2_right(data, date);
  } else if (!collision && data->collapse_state.date_collapsed) {
    // No collision detected and is collapsed -> try to uncollapse it
    uncollapse_line2_right(data, date);
  }
}

// Handle line 6 collision: evaluate if collapse/uncollapse is needed
// Called whenever steps or battery updates
static void handle_line6_update(SlidingTextData *data, const char *steps_text, int battery_percent) {
  char full_battery[64];
  char num_words[64];
  number_to_words(battery_percent, num_words);
  snprintf(full_battery, 64, "%s pc", num_words);
  
  bool collision = check_line6_collision(data, steps_text, full_battery);
  
  if (collision && !data->collapse_state.battery_collapsed) {
    // Collision detected and not collapsed -> collapse it
    collapse_line6_right(data, battery_percent);
  } else if (!collision && data->collapse_state.battery_collapsed) {
    // No collision detected and is collapsed -> try to uncollapse it
    uncollapse_line6_right(data, battery_percent);
  }
}

static void number_to_words(int num, char *buffer) {
  const char *ones[] = {"", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
  const char *teens[] = {"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
  const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
  
  // Handle negative numbers
  if (num < 0) {
    strcpy(buffer, "minus ");
    number_to_words(-num, buffer + 6);  // Recursively convert the positive part
    return;
  }
  
  if (num == 0) strcpy(buffer, "zero");
  else if (num == 100) strcpy(buffer, "one hundred");
  else if (num < 10) strcpy(buffer, ones[num]);
  else if (num < 20) strcpy(buffer, teens[num - 10]);
  else if (num < 100) {
    int ten = num / 10, one = num % 10;
    if (one == 0) strcpy(buffer, tens[ten]);
    else snprintf(buffer, 64, "%s %s", tens[ten], ones[one]);
  }
}

static void steps_to_significant_figure(int steps, char *buffer) {
  const char *ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
  const char *teens[] = {"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
  const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
  
  if (steps == 0) strcpy(buffer, "zero s");
  else if (steps < 10) snprintf(buffer, 32, "%s s", ones[steps]);
  else if (steps < 20) strcpy(buffer, ((steps + 5) / 10 == 1) ? "one ds" : "two ds");
  else if (steps < 100) {
    int rounded = (steps + 5) / 10;
    snprintf(buffer, 32, "%s ds", (rounded < 10) ? ones[rounded] : "ten");
  } else if (steps < 1000) {
    int rounded = (steps + 50) / 100;
    snprintf(buffer, 32, "%s cs", (rounded < 10) ? ones[rounded] : "ten");
  } else if (steps < 20000) {
    int rounded = (steps + 500) / 1000;
    if (rounded < 10) snprintf(buffer, 32, "%s ks", ones[rounded]);
    else if (rounded < 20) snprintf(buffer, 32, "%s ks", teens[rounded - 10]);
    else strcpy(buffer, "twenty ks");
  } else {
    int rounded = (steps + 500) / 1000, ten = rounded / 10, one = rounded % 10;
    if (one == 0) snprintf(buffer, 32, "%s ks", tens[ten]);
    else snprintf(buffer, 32, "%s %s ks", tens[ten], ones[one]);
  }
}

static void day_of_month_to_words(int day, char *buffer) {
  const char *firsts[] = {"first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth"};
  const char *teen_ths[] = {"tenth", "eleventh", "twelfth", "thirteenth", "fourteenth", "fifteenth", "sixteenth", "seventeenth", "eighteenth", "nineteenth"};
  const char *ten_ths[] = {"", "", "twentieth", "thirtieth", "fortieth", "fiftieth", "sixtieth", "seventieth", "eightieth", "ninetieth"};
  const char *tens[] = {"", "", "twenty", "thirty"};
  
  if (day >= 1 && day <= 9) strcpy(buffer, firsts[day - 1]);
  else if (day >= 10 && day <= 19) strcpy(buffer, teen_ths[day - 10]);
  else if (day >= 20 && day <= 31) {
    int ten = day / 10, one = day % 10;
    if (one == 0) strcpy(buffer, ten_ths[ten]);
    else snprintf(buffer, 32, "%s %s", tens[ten], firsts[one - 1]);
  } else strcpy(buffer, "first");
}

static char get_random_char(void) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  return charset[rand() % (sizeof(charset) - 1)];
}

static void start_hacker_animation(SlidingRow *row, const char *target_text, bool fast_mode, bool force_animate) {
  HackerRowState *hs = &row->hacker_state;
  const char *current_text = text_layer_get_text(row->label);
  bool has_existing = current_text && strlen(current_text) > 0 && !force_animate;
  
  strncpy(hs->target_text, target_text, 63);
  hs->target_text[63] = '\0';
  hs->target_length = strlen(hs->target_text);
  hs->animating = true;
  hs->needs_initial_render = true;
  
  // Normalize iteration counts: use same base for all rows to sync animation end times
  // Fast mode still gets fewer iterations but the range is tighter
  int min_iter = fast_mode ? 3 : HACKER_MIN_ITERATIONS;
  int max_iter = fast_mode ? 5 : HACKER_MAX_ITERATIONS;
  
  for (int i = 0; i < hs->target_length; i++) {
    hs->chars[i].target_char = hs->target_text[i];
    
    if (has_existing && i < (int)strlen(current_text)) {
      hs->chars[i].current_char = current_text[i];
      hs->chars[i].locked = (current_text[i] == hs->target_text[i]);
      hs->chars[i].iterations_left = hs->chars[i].locked ? 0 : min_iter;
    } else {
      hs->chars[i].current_char = get_random_char();
      hs->chars[i].locked = false;
    }
    
    if (!hs->chars[i].locked) {
      float progress = (float)i / (hs->target_length > 1 ? hs->target_length - 1 : 1);
      int base_iter = min_iter + (int)(progress * (max_iter - min_iter));
      hs->chars[i].iterations_left = base_iter + (rand() % 2);
      if (hs->chars[i].iterations_left < min_iter) hs->chars[i].iterations_left = min_iter;
    }
    
    if (hs->target_text[i] == ' ') {
      hs->chars[i].current_char = ' ';
      hs->chars[i].locked = true;
      hs->chars[i].iterations_left = 0;
    }
    
    // Build initial display buffer
    hs->display_buffer[i] = hs->chars[i].locked ? hs->chars[i].target_char : hs->chars[i].current_char;
  }
  hs->display_buffer[hs->target_length] = '\0';
  
  // Immediately render the initial scrambled state - no waiting for timer
  text_layer_set_text(row->label, hs->display_buffer);
}

static bool update_hacker_animation(SlidingRow *row) {
  HackerRowState *hs = &row->hacker_state;
  if (!hs->animating) return false;
  
  bool any_unlocked = false;
  for (int i = 0; i < hs->target_length; i++) {
    if (hs->chars[i].locked) {
      hs->display_buffer[i] = hs->chars[i].target_char;
    } else {
      any_unlocked = true;
      if (hs->chars[i].iterations_left <= 0) {
        hs->chars[i].current_char = hs->chars[i].target_char;
        hs->chars[i].locked = true;
        hs->display_buffer[i] = hs->chars[i].target_char;
      } else {
        hs->chars[i].iterations_left--;
        hs->chars[i].current_char = get_random_char();
        hs->display_buffer[i] = hs->chars[i].current_char;
      }
    }
  }
  
  hs->display_buffer[hs->target_length] = '\0';
  text_layer_set_text(row->label, hs->display_buffer);
  
  if (!any_unlocked) {
    hs->animating = false;
    text_layer_set_text(row->label, hs->target_text);
  }
  
  return any_unlocked;
}

static void init_sliding_row(SlidingTextData *data, SlidingRow *row, GRect pos, GFont font, int delay) {
  row->label = text_layer_create(pos);
  text_layer_set_text_alignment(row->label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  text_layer_set_background_color(row->label, GColorClear);
  text_layer_set_text_color(row->label, GColorWhite);
  if (font) {
    text_layer_set_font(row->label, font);
    row->unchanged_font = true;
  } else row->unchanged_font = false;

  row->state = IN_FRAME;
  row->next_string = NULL;
  row->left_pos = -pos.size.w;
  row->right_pos = pos.size.w;
  row->still_pos = pos.origin.x;
  row->movement_delay = delay;
  row->delay_count = 0;
  row->hacker_state.animating = false;
  row->hacker_state.needs_initial_render = false;
  row->hacker_state.target_length = 0;
  memset(row->hacker_state.target_text, 0, sizeof(row->hacker_state.target_text));
  memset(row->hacker_state.display_buffer, 0, sizeof(row->hacker_state.display_buffer));
  
#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
  GRect frame = layer_get_frame(text_layer_get_layer(row->label));
  frame.origin.x = row->still_pos;
  layer_set_frame(text_layer_get_layer(row->label), frame);
#endif

  data->last_hour = -1;
  data->last_minute = -1;
  data->last_day = -1;
  data->last_battery = -1;
  data->last_temperature = 999;
  data->last_steps = -1;
  data->last_step_update_minute = -1;
  data->weather_changed = false;
}

static void slide_in_text(SlidingTextData *data, SlidingRow *row, char* new_text, bool force_animate) {
#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
  // Skip animation during night mode (midnight to 6am) to conserve battery
  if (is_night_mode()) {
    text_layer_set_text(row->label, new_text);
    row->hacker_state.animating = false;
    strncpy(row->hacker_state.target_text, new_text, 63);
    row->hacker_state.target_text[63] = '\0';
    return;
  }
  start_hacker_animation(row, new_text, false, force_animate);
  make_animation();
#else
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
#endif
}

static bool update_sliding_row(SlidingTextData *data, SlidingRow *row) {
#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
  (void) data;
  return update_hacker_animation(row);
#else
  GRect frame = layer_get_frame(text_layer_get_layer(row->label));
  bool changed = true;
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
    default:
      changed = false;
      break;
  }
  if (changed) layer_set_frame(text_layer_get_layer(row->label), frame);
  return changed;
#endif
}

#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
static void hacker_animation_timer(void *data) {
  SlidingTextData *app_data = (SlidingTextData *)data;
  bool any_animating = false;
  any_animating |= update_sliding_row(app_data, &app_data->day_row);
  any_animating |= update_sliding_row(app_data, &app_data->hour_row);
  any_animating |= update_sliding_row(app_data, &app_data->first_minute_row);
  any_animating |= update_sliding_row(app_data, &app_data->second_minute_row);
  any_animating |= update_sliding_row(app_data, &app_data->date_row);
  any_animating |= update_sliding_row(app_data, &app_data->battery_row);
  any_animating |= update_sliding_row(app_data, &app_data->weather_row);
  any_animating |= update_sliding_row(app_data, &app_data->weather_condition_row);
  any_animating |= update_sliding_row(app_data, &app_data->steps_row);
  
  if (any_animating) {
    app_data->hacker_timer = app_timer_register(HACKER_ANIMATION_SPEED_MS, hacker_animation_timer, app_data);
  } else {
    app_data->hacker_timer = NULL;
  }
}
#endif

static void make_animation() {
#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
  if (!s_data->window_ready) return;
  // Skip animation timer during night mode to conserve battery
  if (is_night_mode()) return;
  if (s_data->hacker_timer) app_timer_cancel(s_data->hacker_timer);
  s_data->hacker_timer = app_timer_register(10, hacker_animation_timer, s_data);
#endif
}



static void update_time_display(void) {
  SlidingTextData *data = s_data;
  time_t now = time(NULL);
  struct tm t = *localtime(&now);

  if (data->last_day != t.tm_wday || data->weather_changed) {
    // Generate the full day and date names
    char full_day[32];
    day_to_word(t.tm_wday, full_day);
    
    char full_date[32];
    day_of_month_to_words(t.tm_mday, full_date);
    
    // Get current left-side text for collision checks
    const char *temp_text = text_layer_get_text(data->weather_condition_row.label);
    const char *cond_text = text_layer_get_text(data->weather_row.label);
    
    // Evaluate line 1 collision: temperature (left) vs day (right)
    if (temp_text) {
      if (check_line1_collision(data, temp_text, full_day)) {
        // Collision: use abbreviated day
        strcpy(data->render_state.days[data->render_state.next_days], temp_text);
        day_to_short(t.tm_wday, data->render_state.days[data->render_state.next_days]);
        data->collapse_state.day_collapsed = true;
      } else {
        // No collision: use full day
        strcpy(data->render_state.days[data->render_state.next_days], full_day);
        data->collapse_state.day_collapsed = false;
      }
    } else {
      strcpy(data->render_state.days[data->render_state.next_days], full_day);
      data->collapse_state.day_collapsed = false;
    }
    
    // Evaluate line 2 collision: weather condition (left) vs date (right)
    if (cond_text) {
      if (check_line2_collision(data, cond_text, full_date)) {
        // Collision: use numeric date
        date_to_short(t.tm_mday, data->render_state.dates[data->render_state.next_dates]);
        data->collapse_state.date_collapsed = true;
      } else {
        // No collision: use full date
        strcpy(data->render_state.dates[data->render_state.next_dates], full_date);
        data->collapse_state.date_collapsed = false;
      }
    } else {
      strcpy(data->render_state.dates[data->render_state.next_dates], full_date);
      data->collapse_state.date_collapsed = false;
    }
    
    if (data->last_day != t.tm_wday) {
      slide_in_text(data, &data->day_row, data->render_state.days[data->render_state.next_days], false);
      slide_in_text(data, &data->date_row, data->render_state.dates[data->render_state.next_dates], false);
      data->render_state.next_days = data->render_state.next_days ? 0 : 1;
      data->render_state.next_dates = data->render_state.next_dates ? 0 : 1;
      data->last_day = t.tm_wday;
    }
    if (data->weather_changed) {
      data->weather_changed = false;
    }
  }

  if (data->last_minute != t.tm_min) {
    minute_to_formal_words(t.tm_min, data->render_state.first_minutes[data->render_state.next_minutes], 
                           data->render_state.second_minutes[data->render_state.next_minutes]);
    
    if (t.tm_min > 0 && t.tm_min < 10) {
      strcpy(data->render_state.second_minutes[data->render_state.next_minutes], 
             data->render_state.first_minutes[data->render_state.next_minutes]);
      strcpy(data->render_state.first_minutes[data->render_state.next_minutes], "oh");
    }
    
    // Only animate if text actually changed (force full animation for visibility)
    const char *current_first = text_layer_get_text(data->first_minute_row.label);
    const char *current_second = text_layer_get_text(data->second_minute_row.label);
    
    if (!current_first || strcmp(current_first, data->render_state.first_minutes[data->render_state.next_minutes]) != 0) {
      slide_in_text(data, &data->first_minute_row, data->render_state.first_minutes[data->render_state.next_minutes], true);
    }
    if (!current_second || strcmp(current_second, data->render_state.second_minutes[data->render_state.next_minutes]) != 0) {
      slide_in_text(data, &data->second_minute_row, data->render_state.second_minutes[data->render_state.next_minutes], true);
    }
    
    data->render_state.next_minutes = data->render_state.next_minutes ? 0 : 1;
    data->last_minute = t.tm_min;
  }

  if (data->last_hour != t.tm_hour) {
    hour_to_12h_word(t.tm_hour, data->render_state.hours[data->render_state.next_hours]);
    slide_in_text(data, &data->hour_row, data->render_state.hours[data->render_state.next_hours], false);
    data->render_state.next_hours = data->render_state.next_hours ? 0 : 1;
    data->last_hour = t.tm_hour;
  }
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  (void) units_changed;
  update_time_display();
  if (tick_time->tm_min % 30 == 0) request_weather();
}

static void handle_battery(BatteryChargeState charge_state) {
  SlidingTextData *data = s_data;
  int battery_percent = charge_state.charge_percent;
  
  if (data->last_battery != battery_percent) {
    char full_battery[64];
    char num_words[64];
    number_to_words(battery_percent, num_words);
    snprintf(full_battery, 64, "%s pc", num_words);
    
    const char *steps_text = text_layer_get_text(data->steps_row.label);
    
    // Evaluate line 6 collision: steps (left, gothic18_bold) vs battery (right, gothic18)
    if (steps_text) {
      if (check_line6_collision(data, steps_text, full_battery)) {
        // Collision: use numeric battery
        battery_to_short(battery_percent, data->render_state.battery[data->render_state.next_battery]);
        data->collapse_state.battery_collapsed = true;
      } else {
        // No collision: use full battery
        strcpy(data->render_state.battery[data->render_state.next_battery], full_battery);
        data->collapse_state.battery_collapsed = false;
      }
    } else {
      strcpy(data->render_state.battery[data->render_state.next_battery], full_battery);
      data->collapse_state.battery_collapsed = false;
    }
    
    slide_in_text(data, &data->battery_row, data->render_state.battery[data->render_state.next_battery], false);
    data->render_state.next_battery = data->render_state.next_battery ? 0 : 1;
    data->last_battery = battery_percent;
    make_animation();
  } else {
    // Even if battery percent hasn't changed, check if currently collapsed text can be expanded
    // This handles the case where steps text changed, allowing battery to expand
    const char *steps_text = text_layer_get_text(data->steps_row.label);
    if (steps_text && data->last_battery >= 0) {
      handle_line6_update(data, steps_text, data->last_battery);
    }
  }
}

static void health_handler(HealthEventType event, void *context) {
  (void) context;
  if (event != HealthEventMovementUpdate) return;
  
  SlidingTextData *data = s_data;
  HealthMetric metric = HealthMetricStepCount;
  time_t start = time_start_of_today();
  time_t end = time(NULL);
  
  if (!(health_service_metric_accessible(metric, start, end) & HealthServiceAccessibilityMaskAvailable)) return;
  
  int steps = (int)health_service_sum_today(metric);
  time_t now = time(NULL);
  struct tm t = *localtime(&now);
  
  bool five_minutes_passed = (data->last_step_update_minute == -1) || 
                              (abs(t.tm_min - data->last_step_update_minute) >= 5) ||
                              (data->last_step_update_minute > t.tm_min && (60 - data->last_step_update_minute + t.tm_min) >= 5);
  
  if (data->last_steps != steps && five_minutes_passed) {
    steps_to_significant_figure(steps, data->render_state.steps[data->render_state.next_steps]);
    slide_in_text(data, &data->steps_row, data->render_state.steps[data->render_state.next_steps], false);
    data->render_state.next_steps = data->render_state.next_steps ? 0 : 1;
    data->last_steps = steps;
    data->last_step_update_minute = t.tm_min;
    
    // After steps update, check if battery needs to collapse/uncollapse
    const char *new_steps_text = text_layer_get_text(data->steps_row.label);
    if (new_steps_text && data->last_battery >= 0) {
      handle_line6_update(data, new_steps_text, data->last_battery);
    }
    
    make_animation();
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  (void) context;
  SlidingTextData *data = s_data;
  
  Tuple *temp_tuple = dict_find(iterator, WEATHER_TEMPERATURE_KEY);
  if (temp_tuple) {
    int temperature = (int)temp_tuple->value->int32;
    if (data->last_temperature != temperature) {
      char temp_words[32];
      number_to_words(temperature, temp_words);
      snprintf(data->render_state.temperature[data->render_state.next_temperature], 32, "%s c", temp_words);
      
      // Evaluate line 1 collision: new temperature (left) vs day (right)
      time_t now = time(NULL);
      struct tm t = *localtime(&now);
      char full_day[32];
      day_to_word(t.tm_wday, full_day);
      
      if (check_line1_collision(data, data->render_state.temperature[data->render_state.next_temperature], full_day)) {
        // Collision: use abbreviated day
        day_to_short(t.tm_wday, data->render_state.days[data->render_state.next_days]);
        slide_in_text(data, &data->day_row, data->render_state.days[data->render_state.next_days], false);
        data->render_state.next_days = data->render_state.next_days ? 0 : 1;
        data->collapse_state.day_collapsed = true;
      } else {
        // No collision: try to expand day if it was previously collapsed
        if (data->collapse_state.day_collapsed) {
          strcpy(data->render_state.days[data->render_state.next_days], full_day);
          slide_in_text(data, &data->day_row, data->render_state.days[data->render_state.next_days], false);
          data->render_state.next_days = data->render_state.next_days ? 0 : 1;
          data->collapse_state.day_collapsed = false;
        }
      }
      
      slide_in_text(data, &data->weather_condition_row, data->render_state.temperature[data->render_state.next_temperature], false);
      data->render_state.next_temperature = data->render_state.next_temperature ? 0 : 1;
      data->last_temperature = temperature;
      persist_write_int(PERSIST_WEATHER_TEMPERATURE, temperature);
      data->weather_changed = true;
      make_animation();
    }
  }
  
  Tuple *condition_tuple = dict_find(iterator, WEATHER_CITY_KEY);
  if (condition_tuple) {
    strncpy(data->render_state.weather_condition[data->render_state.next_weather_condition], 
            condition_tuple->value->cstring, 31);
    data->render_state.weather_condition[data->render_state.next_weather_condition][31] = '\0';
    persist_write_string(PERSIST_WEATHER_CONDITION, 
                        data->render_state.weather_condition[data->render_state.next_weather_condition]);
    
    // Evaluate line 2 collision: new weather condition (left) vs date (right)
    time_t now = time(NULL);
    struct tm t = *localtime(&now);
    char full_date[32];
    day_of_month_to_words(t.tm_mday, full_date);
    
    if (check_line2_collision(data, data->render_state.weather_condition[data->render_state.next_weather_condition], full_date)) {
      // Collision: use numeric date
      date_to_short(t.tm_mday, data->render_state.dates[data->render_state.next_dates]);
      slide_in_text(data, &data->date_row, data->render_state.dates[data->render_state.next_dates], false);
      data->render_state.next_dates = data->render_state.next_dates ? 0 : 1;
      data->collapse_state.date_collapsed = true;
    } else {
      // No collision: try to expand date if it was previously collapsed
      if (data->collapse_state.date_collapsed) {
        strcpy(data->render_state.dates[data->render_state.next_dates], full_date);
        slide_in_text(data, &data->date_row, data->render_state.dates[data->render_state.next_dates], false);
        data->render_state.next_dates = data->render_state.next_dates ? 0 : 1;
        data->collapse_state.date_collapsed = false;
      }
    }
    
    slide_in_text(data, &data->weather_row, 
                  data->render_state.weather_condition[data->render_state.next_weather_condition], false);
    data->render_state.next_weather_condition = data->render_state.next_weather_condition ? 0 : 1;
    data->weather_changed = true;
    make_animation();
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) { (void)reason; (void)context; }
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) { (void)iterator; (void)reason; (void)context; }
static void outbox_sent_callback(DictionaryIterator *iterator, void *context) { (void)iterator; (void)context; }

static void request_weather(void) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (!iter) return;
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
#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
  if (s_data->hacker_timer) app_timer_cancel(s_data->hacker_timer);
#endif
  free(s_data);
}

static void handle_init() {
  SlidingTextData *data = (SlidingTextData*)malloc(sizeof(SlidingTextData));
  s_data = data;
  srand(time(NULL));
  data->hacker_timer = NULL;
  data->window_ready = false;
  data->render_state.next_hours = 0;
  data->render_state.next_minutes = 0;
  data->render_state.next_days = 0;
  data->render_state.next_dates = 0;
  data->render_state.next_battery = 0;
  data->render_state.next_temperature = 0;
  data->render_state.next_weather_condition = 0;
  data->render_state.next_steps = 0;

  // Initialize collapse state - assume all expanded initially
  data->collapse_state.day_collapsed = false;
  data->collapse_state.date_collapsed = false;
  data->collapse_state.battery_collapsed = false;

  data->window = window_create();
  window_set_background_color(data->window, GColorBlack);

  data->bitham42_bold = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  data->bitham42_light = fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
  data->gothic18_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  data->gothic18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);

  Layer *window_layer = window_get_root_layer(data->window);
  const int16_t width = layer_get_bounds(window_layer).size.w;
  const int16_t padding = 5;

  init_sliding_row(data, &data->day_row, GRect(2, -2, width - padding, 60), data->gothic18_bold, 6);
  text_layer_set_text_alignment(data->day_row.label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(data->day_row.label));

  init_sliding_row(data, &data->date_row, GRect(2, 14, width - padding, 60), data->gothic18, 6);
  text_layer_set_text_alignment(data->date_row.label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(data->date_row.label));

  init_sliding_row(data, &data->hour_row, GRect(2, 26, width, 60), data->bitham42_bold, 6);
  layer_add_child(window_layer, text_layer_get_layer(data->hour_row.label));

  init_sliding_row(data, &data->first_minute_row, GRect(2, 62, width, 96), data->bitham42_light, 3);
  layer_add_child(window_layer, text_layer_get_layer(data->first_minute_row.label));

  init_sliding_row(data, &data->second_minute_row, GRect(2, 98, width, 132), data->bitham42_light, 0);
  layer_add_child(window_layer, text_layer_get_layer(data->second_minute_row.label));

  init_sliding_row(data, &data->steps_row, GRect(2, 144, width / 2, 168), data->gothic18_bold, 6);
  text_layer_set_text_alignment(data->steps_row.label, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(data->steps_row.label));

  init_sliding_row(data, &data->battery_row, GRect(2, 144, width - padding, 168), data->gothic18, 6);
  text_layer_set_text_alignment(data->battery_row.label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(data->battery_row.label));

  init_sliding_row(data, &data->weather_condition_row, GRect(2, -2, (width * 3) / 4, 60), data->gothic18_bold, 6);
  text_layer_set_text_alignment(data->weather_condition_row.label, GTextAlignmentLeft);
  text_layer_set_overflow_mode(data->weather_condition_row.label, GTextOverflowModeTrailingEllipsis);
  layer_add_child(window_layer, text_layer_get_layer(data->weather_condition_row.label));

  init_sliding_row(data, &data->weather_row, GRect(2, 14, (width * 3) / 4, 60), data->gothic18, 6);
  text_layer_set_text_alignment(data->weather_row.label, GTextAlignmentLeft);
  text_layer_set_overflow_mode(data->weather_row.label, GTextOverflowModeTrailingEllipsis);
  layer_add_child(window_layer, text_layer_get_layer(data->weather_row.label));

  if (persist_exists(PERSIST_WEATHER_TEMPERATURE)) {
    int cached_temp = persist_read_int(PERSIST_WEATHER_TEMPERATURE);
    char temp_words[32];
    number_to_words(cached_temp, temp_words);
    snprintf(data->render_state.temperature[data->render_state.next_temperature], 32, "%s c", temp_words);
    data->last_temperature = cached_temp;
  }
  
  if (persist_exists(PERSIST_WEATHER_CONDITION)) {
    char cached_condition[32];
    persist_read_string(PERSIST_WEATHER_CONDITION, cached_condition, sizeof(cached_condition));
    strncpy(data->render_state.weather_condition[data->render_state.next_weather_condition], cached_condition, 31);
    data->render_state.weather_condition[data->render_state.next_weather_condition][31] = '\0';
  }

  time_t now = time(NULL);
  struct tm t = *localtime(&now);
  
  hour_to_12h_word(t.tm_hour, data->render_state.hours[data->render_state.next_hours]);
  minute_to_formal_words(t.tm_min, data->render_state.first_minutes[data->render_state.next_minutes], 
                        data->render_state.second_minutes[data->render_state.next_minutes]);
  
  if (t.tm_min > 0 && t.tm_min < 10) {
    strcpy(data->render_state.second_minutes[data->render_state.next_minutes], 
           data->render_state.first_minutes[data->render_state.next_minutes]);
    strcpy(data->render_state.first_minutes[data->render_state.next_minutes], "oh");
  }
  
  day_to_word(t.tm_wday, data->render_state.days[data->render_state.next_days]);
  day_of_month_to_words(t.tm_mday, data->render_state.dates[data->render_state.next_dates]);
  
  data->last_hour = t.tm_hour;
  data->last_minute = t.tm_min;
  data->last_day = t.tm_wday;

  // Subscribe to services
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  battery_state_service_subscribe(handle_battery);
  handle_battery(battery_state_service_peek());

#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
  health_handler(HealthEventMovementUpdate, NULL);
#endif

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(256, 256);

  window_set_window_handlers(data->window, (WindowHandlers) {
    .appear = window_appear_handler
  });
  
  window_stack_push(data->window, true);
}

// Called when window appears - this is after OS transition animations complete
static void window_appear_handler(Window *window) {
  (void)window;
  SlidingTextData *data = s_data;
  
  // Mark window as ready for animations
  data->window_ready = true;
  
#if ANIMATION_STYLE == ANIMATION_STYLE_HACKER
  // During night mode (midnight to 6am), skip animations to conserve battery
  if (is_night_mode()) {
    // Just set all text directly without animation
    text_layer_set_text(data->day_row.label, data->render_state.days[data->render_state.next_days]);
    text_layer_set_text(data->date_row.label, data->render_state.dates[data->render_state.next_dates]);
    text_layer_set_text(data->hour_row.label, data->render_state.hours[data->render_state.next_hours]);
    text_layer_set_text(data->first_minute_row.label, data->render_state.first_minutes[data->render_state.next_minutes]);
    text_layer_set_text(data->second_minute_row.label, data->render_state.second_minutes[data->render_state.next_minutes]);
    
    if (persist_exists(PERSIST_WEATHER_TEMPERATURE)) {
      text_layer_set_text(data->weather_condition_row.label, data->render_state.temperature[data->render_state.next_temperature]);
    }
    if (persist_exists(PERSIST_WEATHER_CONDITION)) {
      text_layer_set_text(data->weather_row.label, data->render_state.weather_condition[data->render_state.next_weather_condition]);
    }
    if (data->last_battery >= 0) {
      uint8_t battery_idx = data->render_state.next_battery ? 0 : 1;
      text_layer_set_text(data->battery_row.label, data->render_state.battery[battery_idx]);
    }
    if (data->last_steps >= 0) {
      uint8_t steps_idx = data->render_state.next_steps ? 0 : 1;
      text_layer_set_text(data->steps_row.label, data->render_state.steps[steps_idx]);
    }
    return;
  }
  
  // Set up all animations with initial scrambled text visible immediately
  start_hacker_animation(&data->day_row, data->render_state.days[data->render_state.next_days], true, true);
  start_hacker_animation(&data->date_row, data->render_state.dates[data->render_state.next_dates], true, true);
  start_hacker_animation(&data->hour_row, data->render_state.hours[data->render_state.next_hours], false, true);
  start_hacker_animation(&data->first_minute_row, data->render_state.first_minutes[data->render_state.next_minutes], false, true);
  start_hacker_animation(&data->second_minute_row, data->render_state.second_minutes[data->render_state.next_minutes], false, true);
  
  // Animate weather if we have cached data
  if (persist_exists(PERSIST_WEATHER_TEMPERATURE)) {
    start_hacker_animation(&data->weather_condition_row, data->render_state.temperature[data->render_state.next_temperature], true, true);
  }
  if (persist_exists(PERSIST_WEATHER_CONDITION)) {
    start_hacker_animation(&data->weather_row, data->render_state.weather_condition[data->render_state.next_weather_condition], true, true);
  }
  
  // Animate battery if data was collected during init (next_battery toggles after data is set)
  if (data->last_battery >= 0) {
    // Data is in the previous buffer slot since next was toggled
    uint8_t battery_idx = data->render_state.next_battery ? 0 : 1;
    start_hacker_animation(&data->battery_row, data->render_state.battery[battery_idx], true, true);
  }
  
  // Animate steps if data was collected during init
  if (data->last_steps >= 0) {
    uint8_t steps_idx = data->render_state.next_steps ? 0 : 1;
    start_hacker_animation(&data->steps_row, data->render_state.steps[steps_idx], true, true);
  }
  
  // Start the animation timer with minimal delay
  make_animation();
#endif
}

int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}
