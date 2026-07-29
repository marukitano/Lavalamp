#include <pebble.h>

#include "mytypes.h"
#include "rand.h"

// -----------------------------------------------------------------------------
// Display and animation
// -----------------------------------------------------------------------------

#define DISPLAY_WIDTH PBL_DISPLAY_WIDTH
#define DISPLAY_HEIGHT PBL_DISPLAY_HEIGHT

#define ORIGINAL_WIDTH 144
#define ORIGINAL_HEIGHT 168
#define SCALE_X(value) ((value) * DISPLAY_WIDTH / ORIGINAL_WIDTH)
#define SCALE_Y(value) ((value) * DISPLAY_HEIGHT / ORIGINAL_HEIGHT)

#define NUM_PARTICLES 10
#define NUM_CLOCK_BITS 10
#define NUM_VALUE_FONTS 6

#define REFRESH_RATE_MS 50
#define LABEL_FADE_STEP 16
#define LABEL_OPACITY_MAX 255
#define VALUE_OPTICAL_Y_OFFSET (-7)

#define VALUES_VISIBLE_MS 4000

#define APP_MESSAGE_BUFFER_SIZE 128
#define SETTINGS_PERSIST_KEY 1

// -----------------------------------------------------------------------------
// Fixed-point arithmetic
// -----------------------------------------------------------------------------

#define FIX_BITS 10
#define FIX_ONE (1 << FIX_BITS)
#define FIX_MULT(a, b) (((a) * (b)) >> FIX_BITS)
#define FIX_TO_INT(a) (((a) + (1 << (FIX_BITS - 1))) >> FIX_BITS)
#define INT_TO_FIX(a) ((a) * FIX_ONE)

// -----------------------------------------------------------------------------
// Blob rendering
// -----------------------------------------------------------------------------

#define KERNEL_TABLE_RADIUS 40

// Scales the original 40-pixel radius proportionally to the display size.
#define KERNEL_RADIUS \
    ((KERNEL_TABLE_RADIUS * (DISPLAY_WIDTH + DISPLAY_HEIGHT) + 156) / 312)

#define AA_SAMPLE_OFFSET (FIX_ONE / 4)
#define AA_CHECK_BAND (FIX_ONE / 10)
#define BLOB_THRESHOLD FIX_ONE

#if defined(PBL_PLATFORM_EMERY)
#define VALUE_LABEL_WIDTH 104
#define VALUE_LABEL_MAX_HEIGHT 80
#else
#define VALUE_LABEL_WIDTH 72
#define VALUE_LABEL_MAX_HEIGHT 58
#endif

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

typedef struct {
    int x;
    int y;
} Point2;

typedef struct {
    Point2 position;
    Point2 velocity;
} Particle;

typedef struct {
    uint8_t background_argb;
    uint8_t blob_argb;
    uint8_t value_argb;
    uint8_t shake_values;
} LavalampSettings;

// Used once to migrate installations that stored only the three colors.
typedef struct {
    uint8_t background_argb;
    uint8_t blob_argb;
    uint8_t value_argb;
} LegacyLavalampSettings;

// -----------------------------------------------------------------------------
// UI and application state
// -----------------------------------------------------------------------------

static Window *s_window;
static Layer *s_blob_layer;
static AppTimer *s_animation_timer;
static AppTimer *s_hide_values_timer;

static GFont s_value_fonts[NUM_VALUE_FONTS];
static int s_value_font_heights[NUM_CLOCK_BITS][NUM_VALUE_FONTS];

static LavalampSettings s_settings;
static GColor s_background_color;
static GColor s_blob_color;
static GColor s_value_color;
static GColor s_edge_colors[5];

static bool s_blobs_settled;
static int s_label_opacity;
static int s_label_target_opacity;
static int s_target_hour = -1;
static int s_target_minute = -1;

// -----------------------------------------------------------------------------
// Particles and clock targets
// -----------------------------------------------------------------------------

static Particle s_particles[NUM_PARTICLES];
static Point2 s_particle_targets[NUM_PARTICLES];

static const Point2 s_clock_targets[NUM_CLOCK_BITS] = {
    // Hours: 8, 4, 2, 1
    {SCALE_X(27), SCALE_Y(44)},
    {SCALE_X(57), SCALE_Y(24)},
    {SCALE_X(87), SCALE_Y(54)},
    {SCALE_X(117), SCALE_Y(34)},

    // Minutes: 32, 16, 8, 4, 2, 1
    {SCALE_X(17), SCALE_Y(114)},
    {SCALE_X(47), SCALE_Y(94)},
    {SCALE_X(57), SCALE_Y(134)},
    {SCALE_X(87), SCALE_Y(134)},
    {SCALE_X(97), SCALE_Y(94)},
    {SCALE_X(127), SCALE_Y(114)},
};

static const char *const s_target_labels[NUM_CLOCK_BITS] = {
    "8", "4", "2", "1",
    "32", "16", "8", "4", "2", "1",
};

static bool s_active_targets[NUM_CLOCK_BITS];
static uint8_t s_target_particle_count[NUM_CLOCK_BITS];

// Scratch arrays reused by the renderer.
static int s_row_particle_indices[NUM_PARTICLES];
static int s_row_particle_x[NUM_PARTICLES];
static const int s_aa_sample_x_sign[4] = {-1, 1, -1, 1};
static const int s_aa_sample_y_sign[4] = {-1, -1, 1, 1};

// -----------------------------------------------------------------------------
// Blob kernel
// -----------------------------------------------------------------------------

static const int s_blinn_kernel[KERNEL_TABLE_RADIUS] = {
    2048, 2037, 2002, 1946, 1872, 1779, 1673, 1555, 1429, 1298,
    1167, 1037, 911, 791, 680, 577, 485, 403, 331, 269,
    245, 216, 171, 134, 104, 80, 61, 45, 34, 25,
    18, 13, 9, 6, 4, 3, 2, 1, 1, 0,
};

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

static void animation_timer_callback(void *context);
static void hide_values_timer_callback(void *context);
static void tap_handler(AccelAxisType axis, int32_t direction);
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);
static void save_settings(void);

// -----------------------------------------------------------------------------
// Fixed-point vector helpers
// -----------------------------------------------------------------------------

static void point_add(Point2 *point, Point2 other)
{
    point->x += other.x;
    point->y += other.y;
}

static void point_subtract(Point2 *point, Point2 other)
{
    point->x -= other.x;
    point->y -= other.y;
}

static void point_multiply(Point2 *point, Point2 other)
{
    point->x = FIX_MULT(point->x, other.x);
    point->y = FIX_MULT(point->y, other.y);
}

static void point_normalize(Point2 *point)
{
    const int x_distance = ABS(point->x);
    const int y_distance = ABS(point->y);

    if (x_distance == 0 && y_distance == 0) {
        return;
    }

    if (x_distance < y_distance) {
        const int scale = FIX_ONE / y_distance;
        point->x = FIX_MULT(point->x, scale);
        point->y = point->y < 0 ? -FIX_ONE : FIX_ONE;
    } else {
        const int scale = FIX_ONE / x_distance;
        point->y = FIX_MULT(point->y, scale);
        point->x = point->x < 0 ? -FIX_ONE : FIX_ONE;
    }
}

// -----------------------------------------------------------------------------
// Colors and settings
// -----------------------------------------------------------------------------

static int color_component(GColor color, int shift)
{
    return ((color.argb >> shift) & 0x03) * 85;
}

static GColor mix_colors(
    GColor from_color,
    GColor to_color,
    int amount,
    int maximum)
{
    if (amount <= 0) {
        return from_color;
    }

    if (amount >= maximum) {
        return to_color;
    }

    const int from_red = color_component(from_color, 4);
    const int from_green = color_component(from_color, 2);
    const int from_blue = color_component(from_color, 0);

    const int to_red = color_component(to_color, 4);
    const int to_green = color_component(to_color, 2);
    const int to_blue = color_component(to_color, 0);

    const int red =
        (from_red * (maximum - amount) + to_red * amount) / maximum;
    const int green =
        (from_green * (maximum - amount) + to_green * amount) / maximum;
    const int blue =
        (from_blue * (maximum - amount) + to_blue * amount) / maximum;

    return GColorFromRGB(red, green, blue);
}

static void apply_settings(void)
{
    s_background_color = (GColor){.argb = s_settings.background_argb};
    s_blob_color = (GColor){.argb = s_settings.blob_argb};
    s_value_color = (GColor){.argb = s_settings.value_argb};

    for (int coverage = 0; coverage <= 4; coverage++) {
        s_edge_colors[coverage] = mix_colors(
            s_background_color,
            s_blob_color,
            coverage,
            4);
    }

    if (s_window) {
        window_set_background_color(s_window, s_background_color);
    }

    if (s_blob_layer) {
        layer_mark_dirty(s_blob_layer);
    }
}

static void load_settings(void)
{
    s_settings.background_argb = GColorWhite.argb;
    s_settings.blob_argb = GColorBlack.argb;
    s_settings.value_argb = GColorWhite.argb;
    s_settings.shake_values = true;

    if (persist_exists(SETTINGS_PERSIST_KEY)) {
        const int stored_size = persist_get_size(SETTINGS_PERSIST_KEY);

        if (stored_size == (int)sizeof(s_settings)) {
            persist_read_data(
                SETTINGS_PERSIST_KEY,
                &s_settings,
                sizeof(s_settings));
        } else if (stored_size == (int)sizeof(LegacyLavalampSettings)) {
            LegacyLavalampSettings legacy_settings;

            persist_read_data(
                SETTINGS_PERSIST_KEY,
                &legacy_settings,
                sizeof(legacy_settings));

            s_settings.background_argb =
                legacy_settings.background_argb;
            s_settings.blob_argb =
                legacy_settings.blob_argb;
            s_settings.value_argb =
                legacy_settings.value_argb;

            // Existing users keep the current appearance after updating.
            s_settings.shake_values = true;
            save_settings();
        }
    }

    apply_settings();
}

static void save_settings(void)
{
    persist_write_data(
        SETTINGS_PERSIST_KEY,
        &s_settings,
        sizeof(s_settings));
}

static void inbox_received_handler(
    DictionaryIterator *iterator,
    void *context)
{
    (void)context;

    bool changed = false;
    Tuple *tuple = dict_find(iterator, MESSAGE_KEY_BackgroundColor);

    if (tuple) {
        s_settings.background_argb =
            GColorFromHEX(tuple->value->int32).argb;
        changed = true;
    }

    tuple = dict_find(iterator, MESSAGE_KEY_BlobColor);
    if (tuple) {
        s_settings.blob_argb =
            GColorFromHEX(tuple->value->int32).argb;
        changed = true;
    }

    tuple = dict_find(iterator, MESSAGE_KEY_ValueColor);
    if (tuple) {
        s_settings.value_argb =
            GColorFromHEX(tuple->value->int32).argb;
        changed = true;
    }

    tuple = dict_find(iterator, MESSAGE_KEY_ShakeValues);
    if (tuple) {
        s_settings.shake_values =
            tuple->value->int32 != 0;

        if (!s_settings.shake_values) {
            if (s_hide_values_timer) {
                app_timer_cancel(s_hide_values_timer);
                s_hide_values_timer = NULL;
            }

            s_label_opacity = 0;
            s_label_target_opacity = 0;
        }

        changed = true;
    }

    if (changed) {
        save_settings();
        apply_settings();
    }
}

// -----------------------------------------------------------------------------
// Fonts and value labels
// -----------------------------------------------------------------------------

static int value_font_index_for_target(int target)
{
    const int particle_count = s_target_particle_count[target];
    const bool is_two_digit = s_target_labels[target][1] != '\0';

    if (is_two_digit) {
        if (particle_count <= 1) {
            return 0; // Modak 26
        }
        if (particle_count == 2) {
            return 1; // Modak 32
        }
        if (particle_count == 3) {
            return 2; // Modak 36
        }
        return 3; // Modak 42
    }

    if (particle_count <= 1) {
        return 2; // Modak 36
    }
    if (particle_count == 2) {
        return 3; // Modak 42
    }
    if (particle_count == 3) {
        return 4; // Modak 48
    }
    return 5; // Modak 54
}

static void load_value_fonts(void)
{
    static const uint32_t resource_ids[NUM_VALUE_FONTS] = {
        RESOURCE_ID_FONT_MODAK_26,
        RESOURCE_ID_FONT_MODAK_32,
        RESOURCE_ID_FONT_MODAK_36,
        RESOURCE_ID_FONT_MODAK_42,
        RESOURCE_ID_FONT_MODAK_48,
        RESOURCE_ID_FONT_MODAK_54,
    };

    for (int font_index = 0; font_index < NUM_VALUE_FONTS; font_index++) {
        s_value_fonts[font_index] = fonts_load_custom_font(
            resource_get_handle(resource_ids[font_index]));

        for (int target = 0; target < NUM_CLOCK_BITS; target++) {
            const GSize size = graphics_text_layout_get_content_size(
                s_target_labels[target],
                s_value_fonts[font_index],
                GRect(0, 0, VALUE_LABEL_WIDTH, VALUE_LABEL_MAX_HEIGHT),
                GTextOverflowModeFill,
                GTextAlignmentCenter);

            s_value_font_heights[target][font_index] =
                size.h > 0 ? size.h : VALUE_LABEL_MAX_HEIGHT;
        }
    }
}

static void unload_value_fonts(void)
{
    for (int index = 0; index < NUM_VALUE_FONTS; index++) {
        if (s_value_fonts[index]) {
            fonts_unload_custom_font(s_value_fonts[index]);
            s_value_fonts[index] = NULL;
        }
    }
}

// -----------------------------------------------------------------------------
// Blob field calculation
// -----------------------------------------------------------------------------

static int blinn(int distance)
{
    if (distance >= INT_TO_FIX(KERNEL_RADIUS)) {
        return 0;
    }

    const int scaled_distance =
        (distance * KERNEL_TABLE_RADIUS) / KERNEL_RADIUS;
    const int kernel_index = scaled_distance >> FIX_BITS;
    const int fraction = scaled_distance & (FIX_ONE - 1);

    if (kernel_index >= KERNEL_TABLE_RADIUS - 1) {
        const int start_value = s_blinn_kernel[KERNEL_TABLE_RADIUS - 1];
        return start_value - FIX_MULT(start_value, fraction);
    }

    const int start_value = s_blinn_kernel[kernel_index];
    const int end_value = s_blinn_kernel[kernel_index + 1];

    return start_value +
        FIX_MULT(end_value - start_value, fraction);
}

static int particle_influence(Point2 particle_position, Point2 pixel_position)
{
    const int x_distance = ABS(particle_position.x - pixel_position.x);
    const int y_distance = ABS(particle_position.y - pixel_position.y);

    return FIX_MULT(blinn(x_distance), blinn(y_distance));
}

static int total_influence_at(
    Point2 position,
    int first_particle,
    int end_particle)
{
    int total = 0;

    for (int index = first_particle; index < end_particle; index++) {
        total += particle_influence(
            s_particles[s_row_particle_indices[index]].position,
            position);
    }

    return total;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

static void draw_blobs(GContext *context)
{
    graphics_context_set_stroke_color(context, s_blob_color);

    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        int row_particle_count = 0;

        for (int particle = 0; particle < NUM_PARTICLES; particle++) {
            const int distance_y =
                FIX_TO_INT(s_particles[particle].position.y) - y;

            if (distance_y < KERNEL_RADIUS &&
                distance_y > -KERNEL_RADIUS) {
                s_row_particle_indices[row_particle_count++] = particle;
            }
        }

        if (row_particle_count == 0) {
            continue;
        }

        // Sort the relevant particles by x so each pixel only evaluates
        // particles whose kernel can reach that position.
        for (int left = 0; left < row_particle_count - 1; left++) {
            for (int right = left + 1; right < row_particle_count; right++) {
                if (s_particles[s_row_particle_indices[left]].position.x >
                    s_particles[s_row_particle_indices[right]].position.x) {
                    const int temp = s_row_particle_indices[right];
                    s_row_particle_indices[right] =
                        s_row_particle_indices[left];
                    s_row_particle_indices[left] = temp;
                }
            }
        }

        for (int index = 0; index < row_particle_count; index++) {
            s_row_particle_x[index] = FIX_TO_INT(
                s_particles[s_row_particle_indices[index]].position.x);
        }

        int start_x = s_row_particle_x[0] - KERNEL_RADIUS;
        if (start_x < 0) {
            start_x = 0;
        }

        int first_particle = 0;
        int particle_count = 0;

        for (int x = start_x; x < DISPLAY_WIDTH; x++) {
            while (particle_count < row_particle_count &&
                   s_row_particle_x[particle_count] - KERNEL_RADIUS <= x) {
                particle_count++;
            }

            while (first_particle < row_particle_count &&
                   s_row_particle_x[first_particle] + KERNEL_RADIUS <= x) {
                first_particle++;
            }

            const Point2 pixel_position = {
                .x = INT_TO_FIX(x),
                .y = INT_TO_FIX(y),
            };

            const int influence = total_influence_at(
                pixel_position,
                first_particle,
                particle_count);

            if (influence >= BLOB_THRESHOLD + AA_CHECK_BAND) {
                graphics_draw_pixel(context, GPoint(x, y));
                continue;
            }

            if (influence <= BLOB_THRESHOLD - AA_CHECK_BAND) {
                continue;
            }

            int covered_samples = 0;

            for (int sample = 0; sample < 4; sample++) {
                const Point2 sample_position = {
                    .x = pixel_position.x +
                        s_aa_sample_x_sign[sample] * AA_SAMPLE_OFFSET,
                    .y = pixel_position.y +
                        s_aa_sample_y_sign[sample] * AA_SAMPLE_OFFSET,
                };

                if (total_influence_at(
                        sample_position,
                        first_particle,
                        particle_count) > BLOB_THRESHOLD) {
                    covered_samples++;
                }
            }

            if (covered_samples == 4) {
                graphics_draw_pixel(context, GPoint(x, y));
            }
#if defined(PBL_COLOR)
            else if (covered_samples > 0) {
                graphics_context_set_stroke_color(
                    context,
                    s_edge_colors[covered_samples]);
                graphics_draw_pixel(context, GPoint(x, y));
                graphics_context_set_stroke_color(context, s_blob_color);
            }
#else
            else if (covered_samples >= 2) {
                graphics_draw_pixel(context, GPoint(x, y));
            }
#endif
        }
    }
}

static void draw_values(GContext *context)
{
    if (!s_settings.shake_values) {
        return;
    }

    bool should_draw = s_blobs_settled && s_label_opacity > 0;

#if !defined(PBL_COLOR)
    should_draw =
        s_blobs_settled && s_label_opacity >= LABEL_OPACITY_MAX;
#endif

    if (!should_draw) {
        return;
    }

    const GColor label_color = mix_colors(
        s_blob_color,
        s_value_color,
        s_label_opacity,
        LABEL_OPACITY_MAX);

    graphics_context_set_text_color(context, label_color);

    for (int target = 0; target < NUM_CLOCK_BITS; target++) {
        if (!s_active_targets[target]) {
            continue;
        }

        const int font_index = value_font_index_for_target(target);
        const int text_height =
            s_value_font_heights[target][font_index];

        const GRect text_bounds = GRect(
            s_clock_targets[target].x - VALUE_LABEL_WIDTH / 2,
            s_clock_targets[target].y - text_height / 2 +
                VALUE_OPTICAL_Y_OFFSET,
            VALUE_LABEL_WIDTH,
            text_height + 2);

        graphics_draw_text(
            context,
            s_target_labels[target],
            s_value_fonts[font_index],
            text_bounds,
            GTextOverflowModeFill,
            GTextAlignmentCenter,
            NULL);
    }
}

static void blob_layer_update(Layer *layer, GContext *context)
{
    graphics_context_set_fill_color(context, s_background_color);
    graphics_fill_rect(
        context,
        layer_get_bounds(layer),
        0,
        GCornerNone);

    draw_blobs(context);
    draw_values(context);
}

// -----------------------------------------------------------------------------
// Particle physics
// -----------------------------------------------------------------------------

static void bounce_particle(Particle *particle)
{
    if (particle->position.x < 0) {
        particle->velocity.x = ABS(particle->velocity.x);
        particle->position.x = -particle->position.x;
    } else if (particle->position.x > INT_TO_FIX(DISPLAY_WIDTH)) {
        particle->velocity.x = -ABS(particle->velocity.x);
        particle->position.x =
            INT_TO_FIX(2 * DISPLAY_WIDTH) - particle->position.x;
    }

    if (particle->position.y < 0) {
        particle->velocity.y = ABS(particle->velocity.y);
        particle->position.y = -particle->position.y;
    } else if (particle->position.y > INT_TO_FIX(DISPLAY_HEIGHT)) {
        particle->velocity.y = -ABS(particle->velocity.y);
        particle->position.y =
            INT_TO_FIX(2 * DISPLAY_HEIGHT) - particle->position.y;
    }
}

static void apply_particle_forces(Particle *particle, int particle_index)
{
    Point2 target_force = {
        .x = INT_TO_FIX(s_particle_targets[particle_index].x),
        .y = INT_TO_FIX(s_particle_targets[particle_index].y),
    };

    point_subtract(&target_force, particle->position);

    const int distance = MAX(
        ABS(target_force.x),
        ABS(target_force.y));

    point_normalize(&target_force);

    if (distance < INT_TO_FIX(4)) {
        target_force.x >>= 2;
        target_force.y >>= 2;
    }

    point_add(&particle->velocity, target_force);

    const Point2 drag = {
        .x = (FIX_ONE * 4) / 5,
        .y = (FIX_ONE * 4) / 5,
    };
    point_multiply(&particle->velocity, drag);
}

static bool integrate_particles(void)
{
    for (int index = 0; index < NUM_PARTICLES; index++) {
        Particle *particle = &s_particles[index];

        point_add(&particle->position, particle->velocity);
        bounce_particle(particle);
        apply_particle_forces(particle, index);
    }

    for (int index = 0; index < NUM_PARTICLES; index++) {
        if (FIX_TO_INT(s_particles[index].position.x) !=
                s_particle_targets[index].x ||
            FIX_TO_INT(s_particles[index].position.y) !=
                s_particle_targets[index].y) {
            return true;
        }
    }

    return false;
}

static void initialize_particles(void)
{
    rand_seed();

    for (int index = 0; index < NUM_PARTICLES; index++) {
        s_particles[index].position.x =
            rand_choice(INT_TO_FIX(DISPLAY_WIDTH));
        s_particles[index].position.y =
            rand_choice(INT_TO_FIX(DISPLAY_HEIGHT));
        s_particles[index].velocity.x =
            INT_TO_FIX(rand_range(-3, 3));
        s_particles[index].velocity.y =
            INT_TO_FIX(rand_range(-3, 3));
    }
}

// -----------------------------------------------------------------------------
// Clock target assignment
// -----------------------------------------------------------------------------

static int collect_active_targets(
    const struct tm *tick_time,
    int active_targets[NUM_CLOCK_BITS])
{
    for (int target = 0; target < NUM_CLOCK_BITS; target++) {
        s_active_targets[target] = false;
        s_target_particle_count[target] = 0;
    }

    int active_count = 0;
    int hour = tick_time->tm_hour;

    if (hour > 12) {
        hour -= 12;
    }

    for (int bit = 0; bit < 4; bit++) {
        if (hour & (1 << (3 - bit))) {
            active_targets[active_count++] = bit;
            s_active_targets[bit] = true;
        }
    }

    for (int bit = 0; bit < 6; bit++) {
        if (tick_time->tm_min & (1 << (5 - bit))) {
            const int target = bit + 4;
            active_targets[active_count++] = target;
            s_active_targets[target] = true;
        }
    }

    return active_count;
}

static void scatter_particle_targets(void)
{
    for (int particle = 0; particle < NUM_PARTICLES; particle++) {
        s_particle_targets[particle].x = rand_choice(DISPLAY_WIDTH);
        s_particle_targets[particle].y = rand_choice(DISPLAY_HEIGHT);
    }
}

static void assign_particle_targets(
    const int active_targets[NUM_CLOCK_BITS],
    int active_count)
{
    if (active_count == 0) {
        scatter_particle_targets();
        return;
    }

    int particle_list[NUM_PARTICLES];
    for (int particle = 0; particle < NUM_PARTICLES; particle++) {
        particle_list[particle] = particle;
    }

    // Give every active clock target at least one particle.
    for (int index = 0; index < active_count; index++) {
        const int random_index = index +
            rand_choice(NUM_PARTICLES - index);

        const int temp = particle_list[random_index];
        particle_list[random_index] = particle_list[index];
        particle_list[index] = temp;

        const int target = active_targets[index];
        s_particle_targets[particle_list[index]] = s_clock_targets[target];
        s_target_particle_count[target]++;
    }

    // Distribute all remaining particles among the active targets.
    for (int index = active_count; index < NUM_PARTICLES; index++) {
        const int target = active_targets[rand_choice(active_count)];
        s_particle_targets[particle_list[index]] = s_clock_targets[target];
        s_target_particle_count[target]++;
    }
}

static void schedule_animation_timer(void)
{
    if (!s_animation_timer) {
        s_animation_timer = app_timer_register(
            REFRESH_RATE_MS,
            animation_timer_callback,
            NULL);
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed)
{
    (void)units_changed;

    if (tick_time->tm_hour == s_target_hour &&
        tick_time->tm_min == s_target_minute) {
        return;
    }

    s_target_hour = tick_time->tm_hour;
    s_target_minute = tick_time->tm_min;
    s_blobs_settled = false;
    s_label_opacity = 0;
    s_label_target_opacity = 0;

    if (s_hide_values_timer) {
        app_timer_cancel(s_hide_values_timer);
        s_hide_values_timer = NULL;
    }

    int active_targets[NUM_CLOCK_BITS];
    const int active_count = collect_active_targets(
        tick_time,
        active_targets);

    assign_particle_targets(active_targets, active_count);
    schedule_animation_timer();
    layer_mark_dirty(s_blob_layer);
}

// -----------------------------------------------------------------------------
// Animation
// -----------------------------------------------------------------------------

static void schedule_hide_values_timer(void)
{
    if (s_hide_values_timer) {
        app_timer_cancel(s_hide_values_timer);
    }

    s_hide_values_timer = app_timer_register(
        VALUES_VISIBLE_MS,
        hide_values_timer_callback,
        NULL);
}

static void show_values_temporarily(void)
{
    if (!s_settings.shake_values) {
        return;
    }

    if (s_hide_values_timer) {
        app_timer_cancel(s_hide_values_timer);
        s_hide_values_timer = NULL;
    }

    s_label_target_opacity = LABEL_OPACITY_MAX;

    if (s_blobs_settled &&
        s_label_opacity >= LABEL_OPACITY_MAX) {
        schedule_hide_values_timer();
    } else {
        schedule_animation_timer();
    }

    layer_mark_dirty(s_blob_layer);
}

static void hide_values_timer_callback(void *context)
{
    (void)context;
    s_hide_values_timer = NULL;
    s_label_target_opacity = 0;
    schedule_animation_timer();
}

static void tap_handler(AccelAxisType axis, int32_t direction)
{
    (void)axis;
    (void)direction;

    if (!s_settings.shake_values) {
        return;
    }

    // A single accelerometer tap/shake is enough.
    show_values_temporarily();
}

static void animation_timer_callback(void *context)
{
    (void)context;
    s_animation_timer = NULL;

    bool continue_animation = false;

    if (!s_blobs_settled) {
        const bool particles_are_moving = integrate_particles();
        s_label_opacity = 0;

        if (particles_are_moving) {
            continue_animation = true;
        } else {
            s_blobs_settled = true;
            continue_animation =
                s_label_opacity != s_label_target_opacity;
        }
    } else if (s_label_opacity < s_label_target_opacity) {
        s_label_opacity += LABEL_FADE_STEP;

        if (s_label_opacity >= s_label_target_opacity) {
            s_label_opacity = s_label_target_opacity;

            if (s_label_opacity == LABEL_OPACITY_MAX) {
                schedule_hide_values_timer();
            }
        }

        continue_animation =
            s_label_opacity != s_label_target_opacity;
    } else if (s_label_opacity > s_label_target_opacity) {
        s_label_opacity -= LABEL_FADE_STEP;

        if (s_label_opacity < s_label_target_opacity) {
            s_label_opacity = s_label_target_opacity;
        }

        continue_animation =
            s_label_opacity != s_label_target_opacity;
    }

    if (continue_animation) {
        schedule_animation_timer();
    }

    layer_mark_dirty(s_blob_layer);
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

static void initialize_current_time(void)
{
    const time_t now = time(NULL);
    struct tm *current_time = localtime(&now);

    if (current_time) {
        tick_handler(current_time, MINUTE_UNIT);
    } else {
        scatter_particle_targets();
        schedule_animation_timer();
    }
}

static void app_initialize(void)
{
    load_settings();
    load_value_fonts();

    s_window = window_create();
    window_set_background_color(s_window, s_background_color);

    Layer *root_layer = window_get_root_layer(s_window);
    s_blob_layer = layer_create(layer_get_bounds(root_layer));
    layer_set_update_proc(s_blob_layer, blob_layer_update);
    layer_add_child(root_layer, s_blob_layer);

    app_message_register_inbox_received(inbox_received_handler);
    app_message_open(APP_MESSAGE_BUFFER_SIZE, APP_MESSAGE_BUFFER_SIZE);

    initialize_particles();

    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    accel_tap_service_subscribe(tap_handler);
    initialize_current_time();
}

static void app_deinitialize(void)
{
    tick_timer_service_unsubscribe();
    accel_tap_service_unsubscribe();
    app_message_deregister_callbacks();

    if (s_animation_timer) {
        app_timer_cancel(s_animation_timer);
        s_animation_timer = NULL;
    }

    if (s_hide_values_timer) {
        app_timer_cancel(s_hide_values_timer);
        s_hide_values_timer = NULL;
    }

    if (s_blob_layer) {
        layer_destroy(s_blob_layer);
        s_blob_layer = NULL;
    }

    if (s_window) {
        window_destroy(s_window);
        s_window = NULL;
    }

    unload_value_fonts();
}

int main(void)
{
    app_initialize();
    app_event_loop();
    app_deinitialize();
}
