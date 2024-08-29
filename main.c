#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver.h"

// Pass /dev/ttyUSBx as first positional argument
// Pass devnum as second positional argument
// As third positional argument, pass
//  0 for no input draining
//  1 for serial draining
int main(int argc, char *argv[]) {
    assert(argc == 4);
    
    int fd = ucd_driver_attach(argv[1], atoi(argv[2]), atoi(argv[3]));
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