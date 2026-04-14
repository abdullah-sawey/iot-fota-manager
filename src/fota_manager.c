#include "fota_manager.h"
#include <stdio.h>

#define QSIZE 8

/* ===================== Event Queue ===================== */
static FotaEvent queue[QSIZE];
static int head;
static int tail;

/* ===================== FSM State ===================== */
static FotaState state;

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
}

/* ===================== FSM Update ===================== */
static void fota_update(FotaEvent e)
{
    switch (state) {

    case ST_APP:
        if (e == EV_CHECK)
            state = ST_CHECK;
        break;

    case ST_CHECK:
        if (e == EV_FOUND)
            state = ST_DOWNLOAD;
        break;

    case ST_DOWNLOAD:
        if (e == EV_DOWNLOADED)
            state = ST_VERIFY;
        break;

    case ST_VERIFY:
        if (e == EV_VERIFIED)
            state = ST_APPLY;
        break;

    case ST_APPLY:
        if (e == EV_APPLIED)
            state = ST_APP;
        break;

    default:
        break;
    }
}

/* ===================== FSM Execute ===================== */
static void fota_execute(void)
{
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
