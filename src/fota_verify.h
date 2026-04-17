#ifndef FOTA_VERIFY_H
#define FOTA_VERIFY_H

#include <stdbool.h>
#include <stdint.h>

/* ===================== Public API ===================== */
bool fota_verify_image(const uint8_t* data, int size);
bool fota_verify_signature(const uint8_t* data, int size);

#endif /* FOTA_VERIFY_H */
