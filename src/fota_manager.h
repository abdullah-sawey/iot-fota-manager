#ifndef FOTA_MANAGER_H
#define FOTA_MANAGER_H

#include <stdbool.h>

/* ===================== Events ===================== */
typedef enum {
    EV_CHECK,
    EV_FOUND,
    EV_DOWNLOADED,
    EV_VERIFIED,
    EV_APPLIED,
    EV_ERROR,
    EV_RETRY
} FotaEvent;

/* ===================== States ===================== */
typedef enum {
    ST_APP,
    ST_CHECK,
    ST_DOWNLOAD,
    ST_VERIFY,
    ST_APPLY
} FotaState;

/* ===================== Public API ===================== */
void fota_init(void);
bool fota_enqueue(FotaEvent event);
void fota_process(void);

#endif /* FOTA_MANAGER_H */
