#include "voice_fsm.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "audio_player.h"
#include "voice_events.h"
#include "genie_overlay.h"

/* MultiNet */
#include "asr_multinet.h"

static const char *TAG = "VOICE_FSM";

/* ============================== */
/*           CONFIG               */
/* ============================== */

#define VOICE_POST_GUARD_MS            300
#define VOICE_WAKE_SESSION_TIMEOUT_MS  8000

/* ============================== */
/*           STATE                */
/* ============================== */

static struct {
    voice_fsm_state_t st;
    uint32_t          post_guard_deadline_ms;
} s_diag;

static TaskHandle_t s_task = NULL;
static bool         s_expect_player_done = false;
static bool         s_wake_session_active = false;
static uint32_t     s_wake_deadline_ms = 0;

/* ============================== */
/*        FORWARD DECLS           */
/* ============================== */

static void enter_idle(void);
static void enter_speaking(void);
static void enter_post_guard(void);

static void try_start_wake_reply(void);
static void try_start_multinet_session(void);

/* ============================== */
/*     MULTINET CALLBACK          */
/* ============================== */

static void on_mn_result(const asr_cmd_result_t *r, void *user_ctx)
{
    (void)user_ctx;
    if (!r) return;

    ESP_LOGI(TAG,
             "MN RESULT: cmd=%d label='%s' prob=%.3f",
             (int)r->cmd,
             r->label,
             (double)r->prob);

    // завершить wake-сессию в любом случае
    s_wake_session_active = false;
    s_wake_deadline_ms = 0;
    asr_multinet_stop_session();
    genie_overlay_set_enabled(false);

    // timeout/no-cmd от MultiNet -> озвучиваем как NO_CMD_TIMEOUT и уходим по FSM
    if (r->cmd == ASR_CMD_NONE) {
        esp_err_t err = voice_event_post(VOICE_EVT_NO_CMD_TIMEOUT);
        if (err == ESP_OK) {
            enter_speaking();
        } else {
            ESP_LOGW(TAG, "NO_CMD_TIMEOUT skipped (player busy?)");
            enter_idle();
        }
        return;
    }

    // пока без выполнения команд: просто подтверждаем команду "ок"
    esp_err_t err = voice_event_post(VOICE_EVT_CMD_OK);
    if (err == ESP_OK) {
        enter_speaking();
    } else {
        ESP_LOGW(TAG, "CMD_OK skipped (player busy?)");
        enter_idle();
    }


}

/* ============================== */
/*        AUDIO CALLBACK          */
/* ============================== */

static void player_done_cb(const char *uri,
                           audio_player_done_reason_t reason,
                           void *user_ctx)
{
    (void)uri;
    (void)reason;
    (void)user_ctx;

    if (!s_expect_player_done) {
        ESP_LOGD(TAG, "player_done_cb ignored");
        return;
    }


    s_expect_player_done = false;
    enter_post_guard();
}

/* ============================== */
/*         FSM ENTER              */
/* ============================== */

static void enter_idle(void)
{
    s_diag.st = VOICE_FSM_ST_IDLE;
    s_expect_player_done = false;

    if (!s_wake_session_active) {
        genie_overlay_set_enabled(false);
    }

    try_start_multinet_session();
}

static void enter_speaking(void)
{
    s_diag.st = VOICE_FSM_ST_SPEAKING;

    /* Не слушаем сами себя */
    asr_multinet_stop_session();

    s_expect_player_done = true;
}

static void enter_post_guard(void)
{
    s_diag.st = VOICE_FSM_ST_POST_GUARD;
    s_diag.post_guard_deadline_ms =
        esp_log_timestamp() + VOICE_POST_GUARD_MS;
}

/* ============================== */
/*        WAKE LOGIC              */
/* ============================== */

static void try_start_wake_reply(void)
{
    asr_multinet_stop_session();

    esp_err_t err = voice_event_post(VOICE_EVT_WAKE_DETECTED);
    if (err == ESP_OK) {
        enter_speaking();
    } else {
        ESP_LOGW(TAG, "wake reply skipped (player busy?)");
    }
}

static void try_start_multinet_session(void)
{
    if (!s_wake_session_active) return;
    if (s_diag.st != VOICE_FSM_ST_IDLE) return;
    if (asr_multinet_is_active()) return;
    s_wake_deadline_ms = esp_log_timestamp() + VOICE_WAKE_SESSION_TIMEOUT_MS;
    asr_multinet_start_session(0); // внутренний timeout отключён, рулит только voice_fsm


    ESP_LOGI(TAG,
             "MultiNet session started (timeout=%u ms)",
             (unsigned)VOICE_WAKE_SESSION_TIMEOUT_MS);
}

/* ============================== */
/*           FSM TASK             */
/* ============================== */

static void voice_fsm_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "voice_fsm task started");

    for (;;) {
        const uint32_t now_ms = esp_log_timestamp();

        /* ---- wake session timeout ---- */
        if (s_wake_session_active) {
            int32_t rem_ms = (int32_t)(s_wake_deadline_ms - now_ms);
            if (rem_ms <= 0) {
                s_wake_session_active = false;
                asr_multinet_stop_session();
                ESP_LOGW(TAG, "wake timeout: now=%u deadline=%u st=%d",
                (unsigned)now_ms, (unsigned)s_wake_deadline_ms, (int)s_diag.st);

                genie_overlay_set_enabled(false);

                esp_err_t err = voice_event_post(VOICE_EVT_NO_CMD_TIMEOUT);
                if (err == ESP_OK) {
                    enter_speaking(); // важно: ждать player_done и вернуться в idle через post-guard
                } else {
                    ESP_LOGW(TAG, "NO_CMD_TIMEOUT skipped (player busy?)");
                    // fallback: хотя бы вернуться в idle немедленно
                    enter_idle();
                }

            }
        }

        switch (s_diag.st) {
        case VOICE_FSM_ST_IDLE:
            break;

        case VOICE_FSM_ST_SPEAKING:
            break;

        case VOICE_FSM_ST_POST_GUARD:
            if (s_diag.post_guard_deadline_ms &&
                now_ms >= s_diag.post_guard_deadline_ms) {

                s_diag.post_guard_deadline_ms = 0;
                enter_idle();
            }
            break;

        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ============================== */
/*        PUBLIC API              */
/* ============================== */

esp_err_t voice_fsm_init(void)
{
    memset(&s_diag, 0, sizeof(s_diag));

    s_diag.st = VOICE_FSM_ST_IDLE;
    s_wake_session_active = false;

    audio_player_register_done_cb(player_done_cb, NULL);

    asr_multinet_init(on_mn_result, NULL);

    if (!s_task) {
        xTaskCreatePinnedToCore(
            voice_fsm_task,
            "voice_fsm",
            4096,
            NULL,
            5,
            &s_task,
            0);
    }

    ESP_LOGI(TAG, "voice_fsm initialized");
    return ESP_OK;
}

void voice_fsm_on_wake_detected(void)
{
    if (s_wake_session_active) {
        ESP_LOGI(TAG, "wake while session active — ignore");
        return;
    }

    s_wake_session_active = true;
    s_wake_deadline_ms = esp_log_timestamp() + VOICE_WAKE_SESSION_TIMEOUT_MS;



    genie_overlay_set_enabled(true);
    try_start_wake_reply();
}

// Required by wake_wakenet_task.c (linker expects this symbol).
void voice_fsm_on_wake(void)
{
    voice_fsm_on_wake_detected();
}

