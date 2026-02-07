#include "voice_fsm.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "genie_overlay.h"
#include "esp_log.h"
#include "voice_events.h"

/* ---- config ---- */
#define VOICE_FSM_TASK_CORE          (0)
#define VOICE_FSM_TASK_PRIO          (9)
#define VOICE_FSM_TASK_STACK_BYTES   (4096)
#define VOICE_FSM_QUEUE_LEN          (8)

/* Wake session: сколько держим “активную” индикацию после wake,
   если команда так и не поступила. */
#define VOICE_WAKE_SESSION_TIMEOUT_MS   (8000)


/* Post-guard после playback, чтобы не ловить хвосты/эхо. */
#define VOICE_POST_GUARD_MS          (300u)

/* ---- state ---- */
static const char *TAG = "VOICE_FSM";

typedef enum {
    EV_WAKE = 0,
    EV_PLAYER_DONE,
    EV_POST_GUARD_TIMEOUT,
} voice_fsm_ev_type_t;

typedef struct {
    voice_fsm_ev_type_t type;
    audio_player_done_reason_t done_reason;
} voice_fsm_ev_t;

static QueueHandle_t s_q = NULL;
static TaskHandle_t  s_task = NULL;

static voice_fsm_diag_t s_diag = {
    .st = VOICE_FSM_ST_IDLE,
    .wake_seq = 0,
    .speak_seq = 0,
    .done_seq = 0,
    .last_done_reason = AUDIO_PLAYER_DONE_OK,
};

static bool s_expect_player_done = false;

static bool   s_wake_session_active = false;
static int64_t s_wake_session_deadline_ms = 0;



static void post_event_isr_safe(const voice_fsm_ev_t *ev)
{
    if (!s_q || !ev) return;
    (void)xQueueSend(s_q, ev, 0);
}

static void player_done_cb(const char *path, audio_player_done_reason_t reason, void *arg)
{
    (void)path;
    (void)arg;

    voice_fsm_ev_t ev = {
        .type = EV_PLAYER_DONE,
        .done_reason = reason,
    };
    post_event_isr_safe(&ev);
}

static void enter_idle(void)
{
    s_diag.st = VOICE_FSM_ST_IDLE;
    s_expect_player_done = false;

    if (!s_wake_session_active) {
        genie_overlay_set_enabled(false);
    }
}



static void enter_post_guard(void)
{
    s_diag.st = VOICE_FSM_ST_POST_GUARD;

    /* таймер “в лоб”: в M4 достаточно, потом можно заменить на esp_timer */
    vTaskDelay(pdMS_TO_TICKS(VOICE_POST_GUARD_MS));

    voice_fsm_ev_t ev = { .type = EV_POST_GUARD_TIMEOUT, .done_reason = AUDIO_PLAYER_DONE_OK };
    post_event_isr_safe(&ev);
}

static void try_start_wake_reply(void)
{
    /* В M4: у нас нет распознавания команд, поэтому на wake отвечаем “thinking” как ACK. */
    esp_err_t err = voice_event_post(VOICE_EVT_THINKING);
    if (err == ESP_OK) {
        s_diag.st = VOICE_FSM_ST_SPEAKING;
        s_diag.speak_seq++;
        s_expect_player_done = true;
        ESP_LOGI(TAG, "SPEAK: VOICE_EVT_THINKING");
    } else {
        /* Плеер занят или ещё не инициализирован. В M4 просто игнорируем, чтобы не накапливать очередь. */
        ESP_LOGW(TAG, "voice_event_post THINKING failed: %s", esp_err_to_name(err));
        /* остаёмся в IDLE */
    }
}

static void voice_fsm_task(void *arg)
{
    (void)arg;

    voice_fsm_ev_t ev;
    for (;;) {
        TickType_t wait_ticks = portMAX_DELAY;

        if (s_wake_session_active) {
            const int64_t now_ms = esp_timer_get_time() / 1000;
            int64_t rem_ms = s_wake_session_deadline_ms - now_ms;

            if (rem_ms <= 0) {
                /* Таймаут: гасим индикацию и говорим “не услышал команду” */
                s_wake_session_active = false;
                genie_overlay_set_enabled(false);
                (void)voice_event_post(VOICE_EVT_NO_CMD);
                continue;
            }

            wait_ticks = pdMS_TO_TICKS((uint32_t)rem_ms);
            if (wait_ticks < 1) wait_ticks = 1;
        }

        if (xQueueReceive(s_q, &ev, wait_ticks) != pdTRUE) {
            /* timeout ожидания — цикл повторится и проверит дедлайн */
            continue;
        }

        switch (ev.type) {
            case EV_WAKE: {
                const int64_t now_ms = esp_timer_get_time() / 1000;

                s_diag.wake_seq++;

                /* старт/продление wake-сессии */
                s_wake_session_active = true;
                s_wake_session_deadline_ms = now_ms + VOICE_WAKE_SESSION_TIMEOUT_MS;

                /* индикация “лампа слушает” */
                genie_overlay_set_enabled(true);

                if (s_diag.st == VOICE_FSM_ST_IDLE) {
                    try_start_wake_reply();
                } else {
                    ESP_LOGI(TAG, "wake ignored (st=%d)", (int)s_diag.st);
                }
                break;
            }


            case EV_PLAYER_DONE:
                s_diag.done_seq++;
                s_diag.last_done_reason = ev.done_reason;

                if (!s_expect_player_done) {
                    ESP_LOGI(TAG, "player done ignored (not expected), reason=%d", (int)ev.done_reason);
                    break;
                }

                s_expect_player_done = false;

                if (s_diag.st == VOICE_FSM_ST_SPEAKING) {
                    ESP_LOGI(TAG, "player done reason=%d -> post_guard", (int)ev.done_reason);
                    enter_post_guard();
                } else {
                    ESP_LOGW(TAG, "player done while st=%d", (int)s_diag.st);
                    enter_idle();
                }
                break;


            case EV_POST_GUARD_TIMEOUT:
                if (s_diag.st == VOICE_FSM_ST_POST_GUARD) {
                    ESP_LOGI(TAG, "post_guard done -> idle");
                }
                enter_idle();
                break;

            default:
                break;
        }
    }
}

esp_err_t voice_fsm_init(void)
{
    if (!s_q) {
        s_q = xQueueCreate(VOICE_FSM_QUEUE_LEN, sizeof(voice_fsm_ev_t));
        if (!s_q) return ESP_ERR_NO_MEM;
    }

    /* register done callback */
    audio_player_register_done_cb(player_done_cb, NULL);

    if (!s_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            voice_fsm_task,
            "voice_fsm",
            VOICE_FSM_TASK_STACK_BYTES,
            NULL,
            VOICE_FSM_TASK_PRIO,
            &s_task,
            VOICE_FSM_TASK_CORE);

        if (ok != pdPASS) {
            s_task = NULL;
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "voice_fsm started");
    return ESP_OK;
}

void voice_fsm_on_wake(void)
{
    voice_fsm_ev_t ev = { .type = EV_WAKE, .done_reason = AUDIO_PLAYER_DONE_OK };
    post_event_isr_safe(&ev);
}

void voice_fsm_get_diag(voice_fsm_diag_t *out)
{
    if (!out) return;
    *out = s_diag;
}
