#include "fota_transport.h"
#include <stdio.h>

/* ===================== Transport ===================== */
void fota_transport_init(void)
{
    printf("Transport initialized\n");
}

bool fota_transport_download(const char* url)
{
    printf("Downloading from: %s\n", url);
    return true;
}

void fota_transport_cleanup(void)
{
    printf("Transport cleaned up\n");
}
