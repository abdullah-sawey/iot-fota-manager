#ifndef FOTA_TRANSPORT_H
#define FOTA_TRANSPORT_H

#include <stdbool.h>

/* ===================== Public API ===================== */
void fota_transport_init(void);
bool fota_transport_download(const char* url);
void fota_transport_cleanup(void);

#endif /* FOTA_TRANSPORT_H */
