#include <assert.h>
#include <stdio.h>
#include "driver.h"

// Pass /dev/ttyUSBx as argument
int main(int argc, char *argv[]) {
    assert(argc == 2);
    
    int fd = ucd_driver_attach(argv[1]);
    if (fd < 0) {
        return -1;
    }

    printf("Press enter to detach uart_cam_driver from %s\n", argv[1]);
    getc(stdin);

    if (-1 == ucd_driver_detach(fd)) {
        return -1;
    }

    return 0;
}