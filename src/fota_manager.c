#include "fota_manager.h"
#include <stdio.h>

#define QSIZE 8
#define MAX_RETRIES 3

/* ===================== Event Queue ===================== */
static FotaEvent queue[QSIZE];
static int head;
static int tail;

/* ===================== FSM State ===================== */
static FotaState state;
static int retry_count;
static bool error_to_app;

/* ===================== Queue Helpers ===================== */
bool fota_enqueue(FotaEvent e)
{
    int next = (tail + 1) % QSIZE;

    if (next == head) {
        return false; /* Queue full */
    }

    queue[tail] = e;
    tail = next;
    return true;
}

static bool dequeue(FotaEvent* e)
{
    if (head == tail) {
        return false; /* Queue empty */
    }

    *e = queue[head];
    head = (head + 1) % QSIZE;
    return true;
}

/* ===================== Init ===================== */
void fota_init(void)
{
    head = 0;
    tail = 0;
    state = ST_APP;
    retry_count = 0;
    error_to_app = false;
}

/* ===================== FSM Update ===================== */
static void fota_update(FotaEvent e)
{
    error_to_app = false;

    switch (state) {

    case ST_APP:
        if (e == EV_CHECK) {
            state = ST_CHECK;
            retry_count = 0;
        }
        break;

    case ST_CHECK:
        if (e == EV_FOUND) {
            state = ST_DOWNLOAD;
            retry_count = 0;
        } else if (e == EV_RETRY) {
            retry_count++;
            if (retry_count >= MAX_RETRIES) {
                state = ST_APP;
                retry_count = 0;
            }
        } else if (e == EV_ERROR) {
            state = ST_APP;
            retry_count = 0;
            error_to_app = true;
        }
        break;

    case ST_DOWNLOAD:
        if (e == EV_DOWNLOADED) {
            state = ST_VERIFY;
            retry_count = 0;
        } else if (e == EV_RETRY) {
            retry_count++;
            if (retry_count >= MAX_RETRIES) {
                state = ST_APP;
                retry_count = 0;
            }
        } else if (e == EV_ERROR) {
            state = ST_APP;
            retry_count = 0;
            error_to_app = true;
        }
        break;

    case ST_VERIFY:
        if (e == EV_VERIFIED) {
            state = ST_APPLY;
            retry_count = 0;
        } else if (e == EV_RETRY) {
            retry_count++;
            if (retry_count >= MAX_RETRIES) {
                state = ST_APP;
                retry_count = 0;
            }
        } else if (e == EV_ERROR) {
            state = ST_APP;
            retry_count = 0;
            error_to_app = true;
        }
        break;

    case ST_APPLY:
        if (e == EV_APPLIED) {
            state = ST_APP;
            retry_count = 0;
        } else if (e == EV_ERROR) {
            state = ST_APP;
            retry_count = 0;
            error_to_app = true;
        }
        break;

    default:
        break;
    }
}

/* ===================== FSM Execute ===================== */
static void fota_execute(void)
{
    if (error_to_app) {
        printf("ERROR->APP\n");
    }

    switch (state) {
    case ST_APP:      printf("APP\n");      break;
    case ST_CHECK:    printf("CHECK\n");    break;
    case ST_DOWNLOAD: printf("DOWNLOAD\n"); break;
    case ST_VERIFY:   printf("VERIFY\n");   break;
    case ST_APPLY:    printf("APPLY\n");    break;
    default: break;
    }
}

/* ===================== Super-loop Step ===================== */
void fota_process(void)
{
    FotaEvent e;

    if (dequeue(&e)) {
        fota_update(e);
    }

    fota_execute();
}
