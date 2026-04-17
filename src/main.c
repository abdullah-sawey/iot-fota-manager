#include "fota_manager.h"
#include <stdio.h>

int main(void)
{
    int i;

    fota_init();

    fota_enqueue(EV_CHECK);
    fota_enqueue(EV_FOUND);
    fota_enqueue(EV_DOWNLOADED);
    fota_enqueue(EV_VERIFIED);
    fota_enqueue(EV_APPLIED);

    for (i = 0; i < 5; i++) {
        fota_process();
    }

    printf("FOTA demo complete\n");
    return 0;
}
