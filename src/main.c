#include "fota_manager.h"
#include <stdio.h>

int main(void)
{
    int iteration;

    fota_init();

    fota_enqueue(EV_CHECK);
    fota_enqueue(EV_FOUND);
    fota_enqueue(EV_DOWNLOADED);
    fota_enqueue(EV_VERIFIED);
    fota_enqueue(EV_APPLIED);

    for (iteration = 0; iteration < 5; iteration++) {
        fota_process();
    }

    printf("FOTA demo complete\n");
    return 0;
}
