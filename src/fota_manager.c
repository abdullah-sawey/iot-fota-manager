#include "fota_manager.h"
#include <stdio.h>
#include <string.h>

#define QSIZE FOTA_QSIZE

/* ===================== State Handler Type ===================== */

/*
 * State design pattern – each state owns exactly three hooks:
 *
 *   on_enter   fired once when the FSM arrives in this state.
 *   on_event   maps the incoming event to the next FotaState;
 *              returning the current state means "no transition".
 *   on_execute fired every tick while this state is active.
 *
 * The central transition engine (do_transition) calls on_enter and
 * fires the registered callbacks so individual handlers stay clean.
 */
typedef struct {
    void        (*on_enter)  (void);
    FotaState   (*on_event)  (FotaEvent e);
    void        (*on_execute)(void);
} FotaStateHandler;

/* ===================== Event Queue ===================== */

static FotaEvent queue[QSIZE];
static int       head;
static int       tail;

/* ===================== Runtime Status ===================== */

static FotaState      state;
static FotaError      last_error;
static uint8_t        retry_count;
static uint32_t       events_processed;
static FotaVersion    current_version;
static FotaUpdateInfo update_info;

/* ===================== Callbacks ===================== */

static FotaStateChangeCb state_change_cb;
static FotaErrorCb       error_cb;
static FotaExecuteCb     execute_cb;

/* ===================== ST_APP Handler ===================== */

void st_app_enter(void)
{
    printf("[FOTA] enter %s\n", fota_state_to_str(ST_APP));
}

FotaState st_app_event(FotaEvent e)
{
    if (e == EV_CHECK) return ST_CHECK;

    return ST_APP;
}

void st_app_execute(void)
{
    /* idle – nothing to drive */
}

/* ===================== ST_CHECK Handler ===================== */

void st_check_enter(void)
{
    printf("[FOTA] enter %s\n", fota_state_to_str(ST_CHECK));
}

FotaState st_check_event(FotaEvent e)
{
    if (e == EV_FOUND)     return ST_DOWNLOAD;
    if (e == EV_NOT_FOUND) return ST_APP;
    if (e == EV_FAILED)    { last_error = FOTA_ERR_BAD_STATE; return ST_ERROR; }

    return ST_CHECK;
}

void st_check_execute(void)
{
    printf("[FOTA] checking server...\n");
}

/* ===================== ST_DOWNLOAD Handler ===================== */

void st_download_enter(void)
{
    printf("[FOTA] enter %s  url=%s\n",
           fota_state_to_str(ST_DOWNLOAD), update_info.url);
}

FotaState st_download_event(FotaEvent e)
{
    if (e == EV_DOWNLOADED) return ST_VERIFY;
    if (e == EV_FAILED)     { last_error = FOTA_ERR_BAD_STATE; return ST_ERROR; }

    return ST_DOWNLOAD;
}

void st_download_execute(void)
{
    printf("[FOTA] downloading...\n");
}

/* ===================== ST_VERIFY Handler ===================== */

void st_verify_enter(void)
{
    printf("[FOTA] enter %s  hash=%s\n",
           fota_state_to_str(ST_VERIFY), update_info.hash);
}

FotaState st_verify_event(FotaEvent e)
{
    if (e == EV_VERIFIED) return ST_APPLY;
    if (e == EV_FAILED)   { last_error = FOTA_ERR_HASH; return ST_ERROR; }

    return ST_VERIFY;
}

void st_verify_execute(void)
{
    printf("[FOTA] verifying hash...\n");
}

/* ===================== ST_APPLY Handler ===================== */

void st_apply_enter(void)
{
    printf("[FOTA] enter %s\n", fota_state_to_str(ST_APPLY));
}

FotaState st_apply_event(FotaEvent e)
{
    if (e == EV_APPLIED) return ST_REBOOT;
    if (e == EV_FAILED)  { last_error = FOTA_ERR_FLASH; return ST_ERROR; }

    return ST_APPLY;
}

void st_apply_execute(void)
{
    printf("[FOTA] writing flash...\n");
}

/* ===================== ST_REBOOT Handler ===================== */

void st_reboot_enter(void)
{
    printf("[FOTA] enter %s\n", fota_state_to_str(ST_REBOOT));
}

FotaState st_reboot_event(FotaEvent e)
{
    if (e == EV_REBOOTED) return ST_APP;

    return ST_REBOOT;
}

void st_reboot_execute(void)
{
    printf("[FOTA] waiting for reboot...\n");
}

/* ===================== ST_ROLLBACK Handler ===================== */

void st_rollback_enter(void)
{
    printf("[FOTA] enter %s\n", fota_state_to_str(ST_ROLLBACK));
}

FotaState st_rollback_event(FotaEvent e)
{
    if (e == EV_APPLIED) return ST_REBOOT;
    if (e == EV_FAILED)  { last_error = FOTA_ERR_ROLLBACK; return ST_ERROR; }

    return ST_ROLLBACK;
}

void st_rollback_execute(void)
{
    printf("[FOTA] restoring previous image...\n");
}

/* ===================== ST_ERROR Handler ===================== */

void st_error_enter(void)
{
    printf("[FOTA] enter %s  err=%s\n",
           fota_state_to_str(ST_ERROR), fota_error_to_str(last_error));
}

FotaState st_error_event(FotaEvent e)
{
    if (e == EV_ROLLBACK) return ST_ROLLBACK;

    return ST_ERROR;
}

void st_error_execute(void)
{
    printf("[FOTA] halted – send EV_ROLLBACK or EV_RESET to recover\n");
}

/* ===================== State Table ===================== */

static const FotaStateHandler state_table[] = {
    [ST_APP]      = { st_app_enter,      st_app_event,      st_app_execute      },
    [ST_CHECK]    = { st_check_enter,    st_check_event,    st_check_execute    },
    [ST_DOWNLOAD] = { st_download_enter, st_download_event, st_download_execute },
    [ST_VERIFY]   = { st_verify_enter,   st_verify_event,   st_verify_execute   },
    [ST_APPLY]    = { st_apply_enter,    st_apply_event,    st_apply_execute    },
    [ST_REBOOT]   = { st_reboot_enter,   st_reboot_event,   st_reboot_execute   },
    [ST_ROLLBACK] = { st_rollback_enter, st_rollback_event, st_rollback_execute },
    [ST_ERROR]    = { st_error_enter,    st_error_event,    st_error_execute    },
};

/* ===================== Queue Helpers ===================== */

FotaError fota_enqueue(FotaEvent e)
{
    int next = (tail + 1) % QSIZE;

    if (next == head) {
        return FOTA_ERR_QUEUE_FULL; /* Queue full */
    }

    queue[tail] = e;
    tail = next;
    return FOTA_OK;
}

static bool dequeue(FotaEvent *e)
{
    if (head == tail) {
        return false; /* Queue empty */
    }

    *e = queue[head];
    head = (head + 1) % QSIZE;
    return true;
}

/* ===================== Transition Engine ===================== */

static void do_transition(FotaState next)
{
    FotaState prev = state;

    state = next;
    retry_count = 0;

    if (state_change_cb) {
        state_change_cb(prev, next);
    }

    if (next == ST_ERROR && error_cb) {
        error_cb(last_error);
    }

    state_table[state].on_enter();
}

static FotaState intercept_global(FotaEvent e)
{
    if (e == EV_RESET) {
        return ST_APP;
    }

    if (e == EV_TIMEOUT) {
        last_error = FOTA_ERR_TIMEOUT;
        return ST_ERROR;
    }

    if (e == EV_RETRY) {
        retry_count++;
        if (retry_count >= FOTA_MAX_RETRIES) {
            last_error = FOTA_ERR_BAD_STATE;
            return ST_ERROR;
        }
        return state; /* stay; execute() re-runs the stage */
    }

    return state_table[state].on_event(e);
}

/* ===================== Init ===================== */

void fota_init(void)
{
    head = 0;
    tail = 0;
    state = ST_APP;
    last_error = FOTA_OK;
    retry_count = 0;
    events_processed = 0;
    memset(&update_info, 0, sizeof(update_info));

    state_table[ST_APP].on_enter();
}

/* ===================== Super-loop Step ===================== */

void fota_process(void)
{
    FotaEvent e;

    if (dequeue(&e)) {
        FotaState next;

        events_processed++;
        printf("[FOTA] event=%s\n", fota_event_to_str(e));

        next = intercept_global(e);
        if (next != state) {
            do_transition(next);
        }
    }

    state_table[state].on_execute();

    if (execute_cb) {
        execute_cb(state);
    }
}

/* ===================== Reset ===================== */

void fota_reset(void)
{
    FotaState prev = state;

    state = ST_APP;
    last_error = FOTA_OK;
    retry_count = 0;

    if (state_change_cb) {
        state_change_cb(prev, ST_APP);
    }

    state_table[ST_APP].on_enter();
}

/* ===================== Flush Queue ===================== */

void fota_flush_queue(void)
{
    head = 0;
    tail = 0;
}

/* ===================== State Queries ===================== */

FotaState fota_get_state(void)
{
    return state;
}

bool fota_is_idle(void)
{
    return state == ST_APP;
}

bool fota_has_error(void)
{
    return state == ST_ERROR;
}

/* ===================== Status ===================== */

FotaError fota_get_status(FotaStatus *status)
{
    if (!status) {
        return FOTA_ERR_NULL_PTR;
    }

    status->state            = state;
    status->last_error       = last_error;
    status->retry_count      = retry_count;
    status->events_processed = events_processed;
    status->current_version  = current_version;
    status->pending_version  = update_info.version;
    return FOTA_OK;
}

/* ===================== Version / Update Info ===================== */

FotaError fota_set_current_version(const FotaVersion *version)
{
    if (!version) {
        return FOTA_ERR_NULL_PTR;
    }

    current_version = *version;
    return FOTA_OK;
}

FotaError fota_set_update_info(const FotaUpdateInfo *info)
{
    if (!info) {
        return FOTA_ERR_NULL_PTR;
    }

    update_info = *info;
    return FOTA_OK;
}

FotaError fota_get_update_info(FotaUpdateInfo *info)
{
    if (!info) {
        return FOTA_ERR_NULL_PTR;
    }

    *info = update_info;
    return FOTA_OK;
}

/* ===================== Callback Registration ===================== */

void fota_set_state_change_cb(FotaStateChangeCb cb)
{
    state_change_cb = cb;
}

void fota_set_error_cb(FotaErrorCb cb)
{
    error_cb = cb;
}

void fota_set_execute_cb(FotaExecuteCb cb)
{
    execute_cb = cb;
}

/* ===================== String Helpers ===================== */

const char *fota_state_to_str(FotaState s)
{
    switch (s) {
    case ST_APP:      return "ST_APP";
    case ST_CHECK:    return "ST_CHECK";
    case ST_DOWNLOAD: return "ST_DOWNLOAD";
    case ST_VERIFY:   return "ST_VERIFY";
    case ST_APPLY:    return "ST_APPLY";
    case ST_REBOOT:   return "ST_REBOOT";
    case ST_ROLLBACK: return "ST_ROLLBACK";
    case ST_ERROR:    return "ST_ERROR";
    default:          return "ST_UNKNOWN";
    }
}

const char *fota_event_to_str(FotaEvent e)
{
    switch (e) {
    case EV_CHECK:      return "EV_CHECK";
    case EV_FOUND:      return "EV_FOUND";
    case EV_NOT_FOUND:  return "EV_NOT_FOUND";
    case EV_DOWNLOADED: return "EV_DOWNLOADED";
    case EV_VERIFIED:   return "EV_VERIFIED";
    case EV_APPLIED:    return "EV_APPLIED";
    case EV_REBOOTED:   return "EV_REBOOTED";
    case EV_FAILED:     return "EV_FAILED";
    case EV_TIMEOUT:    return "EV_TIMEOUT";
    case EV_ROLLBACK:   return "EV_ROLLBACK";
    case EV_RETRY:      return "EV_RETRY";
    case EV_RESET:      return "EV_RESET";
    default:            return "EV_UNKNOWN";
    }
}

const char *fota_error_to_str(FotaError err)
{
    switch (err) {
    case FOTA_OK:             return "FOTA_OK";
    case FOTA_ERR_QUEUE_FULL: return "FOTA_ERR_QUEUE_FULL";
    case FOTA_ERR_TIMEOUT:    return "FOTA_ERR_TIMEOUT";
    case FOTA_ERR_HASH:       return "FOTA_ERR_HASH";
    case FOTA_ERR_FLASH:      return "FOTA_ERR_FLASH";
    case FOTA_ERR_ROLLBACK:   return "FOTA_ERR_ROLLBACK";
    case FOTA_ERR_NO_UPDATE:  return "FOTA_ERR_NO_UPDATE";
    case FOTA_ERR_BAD_STATE:  return "FOTA_ERR_BAD_STATE";
    case FOTA_ERR_NULL_PTR:   return "FOTA_ERR_NULL_PTR";
    case FOTA_ERR_OVERFLOW:   return "FOTA_ERR_OVERFLOW";
    default:                  return "FOTA_ERR_UNKNOWN";
    }
}

