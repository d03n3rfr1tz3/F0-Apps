// insired by
// https://github.com/esphome/esphome/blob/ac0d921413c3884752193fe568fa82853f0f99e9/esphome/components/ultrasonic/ultrasonic_sensor.cpp
// Ported and modified by @xMasterX

#include <furi.h>
#include <furi_hal_cortex.h>
#include <furi_hal_light.h>
#include <furi_hal_power.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <gui/elements.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

typedef struct {
    NotificationApp* notification;
    FuriMutex* mutex;
    bool have_5v;
    bool measurement_made;
    float echo; // ms
    float distance; // cm
} PluginState;

const NotificationSequence sequence_success = {
    &message_display_backlight_on,
    &message_green_255,
    &message_note_c5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

const NotificationSequence sequence_fail = {
    &message_display_backlight_on,
    &message_red_255,
    &message_note_c1,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

static void render_callback(Canvas* const canvas, void* ctx) {
    furi_assert(canvas);
    furi_assert(ctx);
    
    PluginState* plugin_state = ctx;

    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(canvas, 64, 2, AlignCenter, AlignTop, "HC-SR04 Ultrasonic\nDistance Sensor");
    canvas_set_font(canvas, FontSecondary);

    if(!plugin_state->have_5v) {
        elements_multiline_text_aligned(
            canvas,
            4,
            28,
            AlignLeft,
            AlignTop,
            "5V on GPIO must be\nenabled, or USB must\nbe connected.");
    } else {
        if(!plugin_state->measurement_made) {
            elements_multiline_text_aligned(canvas, 64, 28, AlignCenter, AlignTop, "Press OK button to measure");
            elements_multiline_text_aligned(canvas, 64, 40, AlignCenter, AlignTop, "13/TX -> Trig\n14/RX -> Echo");
        } else {
            FuriString* str_buf;
            str_buf = furi_string_alloc();
            furi_string_printf(str_buf, "Echo: %0.2f ms", (double)(plugin_state->echo));

            canvas_draw_str_aligned(
                canvas, 8, 28, AlignLeft, AlignTop, furi_string_get_cstr(str_buf));
            furi_string_printf(str_buf, "Distance: %0.2f cm", (double)plugin_state->distance);
            canvas_draw_str_aligned(
                canvas, 8, 38, AlignLeft, AlignTop, furi_string_get_cstr(str_buf));
            furi_string_printf(str_buf, "Distance: %0.2f in", (double)(plugin_state->distance/2.54f));
            canvas_draw_str_aligned(
                canvas, 8, 48, AlignLeft, AlignTop, furi_string_get_cstr(str_buf));

            furi_string_free(str_buf);
        }
    }
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(input_event);
    furi_assert(ctx);

    FuriMessageQueue* event_queue = ctx;
    
    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void hc_sr04_state_init(PluginState* const plugin_state) {
    plugin_state->echo = -1;
    plugin_state->distance = -1;
    plugin_state->measurement_made = false;

    furi_hal_power_suppress_charge_enter();

    plugin_state->have_5v = false;
    if(furi_hal_power_is_otg_enabled() || furi_hal_power_is_charging()) {
        plugin_state->have_5v = true;
    } else {
        furi_hal_power_enable_otg();
        plugin_state->have_5v = true;
    }
}

float hc_sr04_duration_to_ms(uint32_t duration) {
    return duration / (SystemCoreClock / 1000000) / 100.0f; 
}

float hc_sr04_duration_to_mm(uint32_t duration) {
    const float temperature = 19.307;
    const float speed_sound_mm_per_ms = (331.3f + 0.606f * temperature) / 1000; // Cair ≈ (331.3 + 0.606 ⋅ ϑ) m/s
    float total_dist = duration / (SystemCoreClock / 1000000) / 2.0f * speed_sound_mm_per_ms;
    return total_dist; 
}

float hc_sr04_duration_to_cm(uint32_t duration) {
    const float temperature = 19.307;
    const float speed_sound_cm_per_ms = (331.3f + 0.606f * temperature) / 1000 / 10; // Cair ≈ (331.3 + 0.606 ⋅ ϑ) m/s
    float total_dist = duration / (SystemCoreClock / 1000000) / 2.0f * speed_sound_cm_per_ms;
    return total_dist; 
}

static void hc_sr04_measure(PluginState* const plugin_state) {
    if(!plugin_state->have_5v) {
        if(furi_hal_power_is_otg_enabled() || furi_hal_power_is_charging()) {
            plugin_state->have_5v = true;
        } else {
            notification_message(plugin_state->notification, &sequence_fail);
            return;
        }
    }

    notification_message(plugin_state->notification, &sequence_blink_start_yellow);

    const uint32_t timeout_ms = 2000;
    // Pin 13 / TX -> Trig
    furi_hal_gpio_write(&gpio_usart_tx, false);
    furi_hal_gpio_init(&gpio_usart_tx, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);

    // Pin 14 / RX -> Echo
    furi_hal_gpio_write(&gpio_usart_rx, false);
    furi_hal_gpio_init(&gpio_usart_rx, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);

    FURI_CRITICAL_ENTER();
    furi_hal_gpio_write(&gpio_usart_tx, true);
    furi_delay_ms(20); // 20 ms pulse on TX
    furi_hal_gpio_write(&gpio_usart_tx, false);

    const uint32_t start = furi_get_tick();

    while(furi_get_tick() - start < timeout_ms && furi_hal_gpio_read(&gpio_usart_rx))
        ;
    while(furi_get_tick() - start < timeout_ms && !furi_hal_gpio_read(&gpio_usart_rx))
        ;

    FuriHalCortexTimer beginTimer = furi_hal_cortex_timer_get(0);

    while(furi_get_tick() - start < timeout_ms && furi_hal_gpio_read(&gpio_usart_rx))
        ;

    FuriHalCortexTimer endTimer = furi_hal_cortex_timer_get(0);
    FURI_CRITICAL_EXIT();

    uint32_t duration = endTimer.start - beginTimer.start;
    plugin_state->echo = hc_sr04_duration_to_ms(duration);
    plugin_state->distance = hc_sr04_duration_to_cm(duration);
    plugin_state->measurement_made = true;

    notification_message(plugin_state->notification, &sequence_blink_stop);
    notification_message(plugin_state->notification, duration > 0 ? &sequence_success : &sequence_fail);
}

int32_t hc_sr04_app() {
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));

    PluginState* plugin_state = malloc(sizeof(PluginState));

    hc_sr04_state_init(plugin_state);

    //furi_hal_console_disable();

    if(!(plugin_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal))) {
        FURI_LOG_E("hc_sr04", "cannot create mutex\r\n");
        if(furi_hal_power_is_otg_enabled()) {
            furi_hal_power_disable_otg();
        }
        //furi_hal_console_enable();
        furi_hal_power_suppress_charge_exit();
        furi_message_queue_free(event_queue);
        free(plugin_state);
        return 255;
    }

    plugin_state->notification = furi_record_open(RECORD_NOTIFICATION);

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, plugin_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    PluginEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);

        if (furi_mutex_acquire(plugin_state->mutex, FuriWaitForever) == FuriStatusOk && event_status == FuriStatusOk) {
            // press events
            if (event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyUp:
                    case InputKeyDown:
                    case InputKeyRight:
                    case InputKeyLeft:
                        break;
                    case InputKeyOk:
                        hc_sr04_measure(plugin_state);
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        view_port_update(view_port);
        furi_mutex_release(plugin_state->mutex);
    }

    if(furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    furi_hal_power_suppress_charge_exit();

    // Return TX / RX back to usart mode
    furi_hal_gpio_init_ex(
        &gpio_usart_tx,
        GpioModeAltFunctionPushPull,
        GpioPullUp,
        GpioSpeedVeryHigh,
        GpioAltFn7USART1);
    furi_hal_gpio_init_ex(
        &gpio_usart_rx,
        GpioModeAltFunctionPushPull,
        GpioPullUp,
        GpioSpeedVeryHigh,
        GpioAltFn7USART1);
    //furi_hal_console_enable();

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(plugin_state->mutex);

    return 0;
}