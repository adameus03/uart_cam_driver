#include <fcntl.h> 

#ifndef __USE_MISC
#define __USE_MISC
#endif // __USE_MISC (this is to make sure cfmakeraw is defined)
//#define __GNU_SOURCE
#include <asm-generic/termbits-common.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#include "log.h"
#include "driver.h"

#define UCD_PATH_PREFIX "/custom/ucd"
//#define _TO_STRING(x) #x
//#define UCD_PATH(devnum) UCD_PATH_PREFIX _TO_STRING(devnum)

#define UCD_REQUEST_GET_FRAME 'f'
#define UCD_REQUEST_UPDATE_FIRMWARE 'u'

#define UCD_STATE_NR '0'
#define UCD_STATE_R '1'

#define UCD_RELATIVE_FILE_REQUEST "request"
#define UCD_RELATIVE_FILE_STATE "state"
#define UCD_RELATIVE_FILE_FRAME "frame"
#define UCD_RELATIVE_FILE_FIRMWARE "firmware"

// #define UCD_FILE_REQUEST(devnum) UCD_PATH(devnum) "/" UCD_RELATIVE_FILE_REQUEST
// #define UCD_FILE_STATE(devnum) UCD_PATH(devnum) "/" UCD_RELATIVE_FILE_STATE
// #define UCD_FILE_FRAME(devnum) UCD_PATH(devnum) "/" UCD_RELATIVE_FILE_FRAME
// #define UCD_FILE_FIRMWARE(devnum) UCD_PATH(devnum) "/" UCD_RELATIVE_FILE_FIRMWARE

#define UCD_BUF_SIZE 30720
//#define UCD_SEND_BUF_SIZE 30720
//#define UCD_RCV_BUF_SIZE 1024
#define UCD_READY_MARKER "wxyz"
#define UCD_READY_MARKER_LEN 4

#define UCD_MAX_CHARS_PER_PATH 100
static char* ucd_dir_path(int devnum) {
    static char path[UCD_MAX_CHARS_PER_PATH+1] = {0};
    if (path[0] == 0) {
        int rv = snprintf(path, UCD_MAX_CHARS_PER_PATH+1, "%s%d", UCD_PATH_PREFIX, devnum);
        assert (rv > 0 && rv <= UCD_MAX_CHARS_PER_PATH);
    }
    return path;
}
static char* ucd_file_path(char** ppcPath, int devnum, const char* relative_file) {
    char* path = *ppcPath;
    if (path[0] == 0) {
        int rv = snprintf(path, UCD_MAX_CHARS_PER_PATH+1, "%s%d/%s", UCD_PATH_PREFIX, devnum, relative_file);
        assert (rv > 0 && rv <= UCD_MAX_CHARS_PER_PATH);
    }
    return path;
}
static char* UCD_PATH(int devnum) {
    return ucd_dir_path(devnum);
}
static char* UCD_FILE_REQUEST(int devnum) {
    static char path[UCD_MAX_CHARS_PER_PATH+1] = {0};
    char* pcPath = path;
    return ucd_file_path(&pcPath, devnum, UCD_RELATIVE_FILE_REQUEST);
}
static char* UCD_FILE_STATE(int devnum) {
    static char path[UCD_MAX_CHARS_PER_PATH+1] = {0};
    char* pcPath = path;
    return ucd_file_path(&pcPath, devnum, UCD_RELATIVE_FILE_STATE);
}
static char* UCD_FILE_FRAME(int devnum) {
    static char path[UCD_MAX_CHARS_PER_PATH+1] = {0};
    char* pcPath = path;
    return ucd_file_path(&pcPath, devnum, UCD_RELATIVE_FILE_FRAME);
}
static char* UCD_FILE_FIRMWARE(int devnum) {
    static char path[UCD_MAX_CHARS_PER_PATH+1] = {0};
    char* pcPath = path;
    return ucd_file_path(&pcPath, devnum, UCD_RELATIVE_FILE_FIRMWARE);
}

static pthread_t __ucd_loop_thread;
static int __ucd_fd = -1;
static int __ucd_devnum = -1;

static int ucd_serial_read(int fd, void* buf, size_t len) {
    size_t bytes_read = 0;
    while (bytes_read < len) {
        ssize_t rv = read(fd, buf + bytes_read, len - bytes_read);
        if (rv < 0) {
            LOG_E("Error reading from serial port: %s", strerror(errno));
            return -1;
        }
        if (rv == 0) {
            LOG_E("Serial port unexpected zero bytes read");
            return -1;
        }
        bytes_read += rv;
    }
    return 0;
}

static int ucd_set_state(char s) {
    int state_fd = open(UCD_FILE_STATE(__ucd_devnum), O_WRONLY);
    // Clean the file
    if (ftruncate(state_fd, 0) != 0) {
        LOG_E("Error truncating %s: %s", UCD_FILE_STATE(__ucd_devnum), strerror(errno));
        return -1;
    }
    if (state_fd < 0) {
        LOG_E("Error %d opening %s: %s", errno, UCD_FILE_STATE(__ucd_devnum), strerror(errno));
        return -1;
    }
    if (write(state_fd, &s, 1) != 1) {
        LOG_E("Error writing state: %s", strerror(errno));
        return -1;
    }
    close(state_fd);
    return 0;
}

static int ucd_create_file(const char* path) {
    int fd = open(path, O_CREAT | O_WRONLY, 0777);
    if (fd < 0) {
        LOG_E("Error %d creating %s: %s", errno, path, strerror(errno));
        return -1;
    } else {
        LOG_D("Created %s", path);
    }
    return 0;
}

static int ucd_delete_file(const char* path) {
    if (access(path, F_OK) == 0) {
        if (-1 == unlink(path)) {
            LOG_E("Error removing %s: %s", path, strerror(errno));
            return -1;
        } else {
            LOG_D("Removed %s", path);
        }
    }
    return 0;
}

static int ucd_create_files() {
    if ( (-1 == ucd_create_file(UCD_FILE_REQUEST(__ucd_devnum)))
        || (-1 == ucd_create_file(UCD_FILE_STATE(__ucd_devnum)))
        || (-1 == ucd_create_file(UCD_FILE_FRAME(__ucd_devnum)))
        || (-1 == ucd_create_file(UCD_FILE_FIRMWARE(__ucd_devnum))) ) {
        return -1;
    }
    if (-1 == ucd_set_state(UCD_STATE_R)) {
        return -1;
    }
    return 0;
}

static int ucd_delete_files() {
    if ( (-1 == ucd_delete_file(UCD_FILE_REQUEST(__ucd_devnum)))
        || (-1 == ucd_delete_file(UCD_FILE_STATE(__ucd_devnum)))
        || (-1 == ucd_delete_file(UCD_FILE_FRAME(__ucd_devnum)))
        || (-1 == ucd_delete_file(UCD_FILE_FIRMWARE(__ucd_devnum))) ) {
        return -1;
    }
    return 0;
}

/**
 * Reads a camera frame from the serial port and writes it to UCD_FILE_FRAME(devnum)
 * @param fd File descriptor for the serial port
 */
static void ucd_frame(int fd) {
    //tcflush(fd, TCIFLUSH);
    LOG_D("Getting frame");
    // Send 'f' to the serial port
    char request = UCD_REQUEST_GET_FRAME;
    LOG_D("Sending request");
    if (write(fd, &request, 1) != 1) {
        LOG_E("Error writing request: %s", strerror(errno));
        return;
    }

    // Receive 4 bytes for the frame size
    //char frame_size_bytes[4];
    uint32_t frame_size = 0U;
    LOG_D("Receiving frame size");


    // LOG_W("Entering a test setup");
    // char c;
    // int r = read(fd, &c, 1);
    // if (r != 1) {
    //     LOG_E("Error reading frame size: %s (errno = %d, rv = %d)", strerror(errno), errno, r);
    //     return;
    // }
    // LOG_D("Received byte: %c", c);
    // r = read(fd, &c, 1);
    // if (r != 1) {
    //     LOG_E("Error reading frame size: %s (errno = %d, rv = %d)", strerror(errno), errno, r);
    //     return;
    // }
    // LOG_D("Received byte: %c", c);
    // r = read(fd, &c, 1);
    // if (r != 1) {
    //     LOG_E("Error reading frame size: %s (errno = %d, rv = %d)", strerror(errno), errno, r);
    //     return;
    // }
    // LOG_D("Received byte: %c", c);
    // r = read(fd, &c, 1);
    // if (r != 1) {
    //     LOG_E("Error reading frame size: %s (errno = %d, rv = %d)", strerror(errno), errno, r);
    //     return;
    // }
    // LOG_D("Received byte: %c", c);
    // LOG_E("Returning from a test setup");
    // return;
    

    // if (read(fd, frame_size_bytes, 4) != 4) {
    //     LOG_E("Error reading frame size: %s", strerror(errno));
    //     return;
    // }
    if (ucd_serial_read(fd, (void*)&frame_size, 4LU) != 0) {
        LOG_E("ucd_serial_read failed");
        return;
    }
    // Convert the 4 bytes to an integer
    // uint32_t frame_size = 0;
    // for (uint8_t i = 0U; i < 4U; i++) {
    //     frame_size |= frame_size_bytes[i] << (i << 3);
    // }
    LOG_D("Frame size is %u B (0x%x B)", frame_size, frame_size);
    // Receive the frame in chunks of UCD_BUF_SIZE bytes (should be 1024) and write them to UCD_FILE_FRAME(devnum)
    char frame_chunk[UCD_BUF_SIZE];
    int frame_fd = open(UCD_FILE_FRAME(__ucd_devnum), O_WRONLY);
    if (frame_fd < 0) {
        LOG_E("Error %d opening %s: %s", errno, UCD_FILE_FRAME(__ucd_devnum), strerror(errno));
        return;
    }
    if (ftruncate(frame_fd, 0) != 0) { // Clean the file
        LOG_E("Error truncating %s: %s", UCD_FILE_FRAME(__ucd_devnum), strerror(errno));
        return;
    }
    for (uint32_t i = 0U; i < frame_size; i += UCD_BUF_SIZE) {
        LOG_D("Reading frame chunk at %d B", i);
        uint32_t bytes_to_read = (frame_size - i) < UCD_BUF_SIZE ? (frame_size - i) : UCD_BUF_SIZE;
        //if (read(fd, frame_chunk, bytes_to_read) != bytes_to_read) {
        if (ucd_serial_read(fd, frame_chunk, bytes_to_read) != 0) {
            //LOG_E("Error reading frame chunk: %s", strerror(errno));
            LOG_E("ucd_serial_read failed.");
            return;
        }
        // if (ucd_serial_read(fd, frame_chunk, bytes_to_read) != 0) {
        //     LOG_E("ucd_serial_read failed");
        //     return;
        // }
        if (write(frame_fd, frame_chunk, bytes_to_read) != bytes_to_read) {
            LOG_E("Error writing frame chunk: %s", strerror(errno));
            return;
        }
    }
    close(frame_fd);
    LOG_D("Done getting frame");
}

static void ucd_firmware_update(int fd) {
    LOG_D("Updating firmware");
    // Send 'u' to the serial port
    char request = UCD_REQUEST_UPDATE_FIRMWARE;
    int firmware_fd = open(UCD_FILE_FIRMWARE(__ucd_devnum), O_RDONLY);
    if (firmware_fd < 0) {
        LOG_E("Error %d opening %s: %s", errno, UCD_FILE_FIRMWARE(__ucd_devnum), strerror(errno));
        return;
    }
    LOG_D("Sending request");
    if (write(__ucd_fd, &request, 1) != 1) {
        LOG_E("Error writing request: %s", strerror(errno));
        return;
    }

    //Obtain size of firmware stored in UCD_FILE_FIRMWARE(devnum)
    struct stat st;
    if (fstat(firmware_fd, &st) != 0) {
        LOG_E("Error getting firmware size: %s", strerror(errno));
        return;
    }
    uint32_t firmware_size = (uint32_t)st.st_size;
    LOG_D("Sending firmware size");
    if (write(fd, (const void*)&firmware_size, 4) != 4) {
        LOG_E("Error writing firmware size: %s", strerror(errno));
        return;
    }
    LOG_D("Firmware size is %d B", firmware_size);
    // Read the firmware in chunks of UCD_BUF_SIZE bytes (should be 1024) from file UCD_FILE_FIRMWARE(devnum) and write them to the serial port
    char firmware_chunk[UCD_BUF_SIZE];
    
    for (uint32_t i = 0U; i < firmware_size; i += UCD_BUF_SIZE) {
        LOG_D("Reading firmware chunk at %d B", i);
        uint32_t bytes_to_read = (firmware_size - i) < UCD_BUF_SIZE ? (firmware_size - i) : UCD_BUF_SIZE;
        if (read(firmware_fd, firmware_chunk, bytes_to_read) != bytes_to_read) {
            LOG_E("Error reading firmware chunk: %s", strerror(errno));
            return;
        }
        if (write(fd, firmware_chunk, bytes_to_read) != bytes_to_read) {
            LOG_E("Error writing firmware chunk: %s", strerror(errno));
            return;
        }
        usleep(100000); // Sleep 100ms so we don't overwhelm the receiver?
        //sleep(1);
    }
    close(firmware_fd);
    LOG_I("Done updating firmware. You may need to upgrade and/or restart this program.");
}

void* ucd_loop(void* pArg) {
    int fd = *((int*) pArg);
    // Read from the serial port until SAU_READY_MARKER is present
    char drain_buf[UCD_READY_MARKER_LEN];
    LOG_D("Draining the input buffer...");
    while (1) {
        //if (read(fd, drain_buf, UCD_READY_MARKER_LEN) == UCD_READY_MARKER_LEN) {
        if (ucd_serial_read(fd, drain_buf, UCD_READY_MARKER_LEN) == 0) {
            if (strncmp(drain_buf, UCD_READY_MARKER, UCD_READY_MARKER_LEN) == 0) {
                // Read single byte to know how many further bytes to drain
                char drain_byte;
                if (read(fd, &drain_byte, 1) != 1) {
                    LOG_E("Error reading from serial port: %s", strerror(errno));
                    return NULL;
                }
                ssize_t bytes_to_drain = ((ssize_t)(drain_byte - '0')) * (1 + UCD_READY_MARKER_LEN);
                if (bytes_to_drain > 0) {
                    char drain_buf[bytes_to_drain];
                    ssize_t rv = read(fd, drain_buf, bytes_to_drain);
                    if (rv != bytes_to_drain) {
                        LOG_E("Error reading from serial port: %s (errno = %d, rv = %d, bytes_to_drain=%d)", strerror(errno), errno, rv, bytes_to_drain);
                        if (rv >= 0) {
                            // print received incomplete buffer
                            char* incomplete_data = (char*)malloc(rv + 1);
                            memcpy(incomplete_data, drain_buf, rv);
                            incomplete_data[rv] = '\0';
                            LOG_E("The incomplete data was: [%s]", incomplete_data); 
                        }
                        return NULL;
                    }
                }
                break;
            }
        } else {
            //LOG_E("Error reading from serial port: %s", strerror(errno));
            LOG_E("ucd_serial_read failed.");
            return NULL;
        }
    }

    LOG_D("Ready to handle requests");
    while (1) {
        // Read request
        char request;
        int request_fd = open(UCD_FILE_REQUEST(__ucd_devnum), O_RDONLY);
        if (request_fd < 0) {
            LOG_E("Error %d opening %s: %s", errno, UCD_FILE_REQUEST(__ucd_devnum), strerror(errno));
            return NULL;
        }
        ssize_t rv = read(request_fd, &request, 1); 
        while (rv == 0) {
            usleep(10000); // Sleep 10ms
            rv = read(request_fd, &request, 1);
        }
        if (rv != 1) {
            LOG_E("Error reading request: %s (errno = %d, rv = %d)", strerror(errno), errno, rv);
            return NULL;
        }
        ucd_set_state(UCD_STATE_NR);
        switch (request) {
            case UCD_REQUEST_GET_FRAME:
                ucd_frame(fd);
                break;
            case UCD_REQUEST_UPDATE_FIRMWARE:
                ucd_firmware_update(fd);
                break;
        }
        close(request_fd);
        request_fd = open(UCD_FILE_REQUEST(__ucd_devnum), O_WRONLY);
        if (0 != ftruncate(request_fd, 0)) {
            LOG_E("Error truncating %s: %s", UCD_FILE_REQUEST(__ucd_devnum), strerror(errno));
            return NULL;
        }
        close(request_fd);
        ucd_set_state(UCD_STATE_R);
    }
}

static int ucd_init(int fd) {
    // Make sure UCD_PATH(__ucd_devnum) directory is existent and empty

    // Check if UCD_PATH(__ucd_devnum) directory exists
    if (access(UCD_PATH(__ucd_devnum), F_OK) != 0) {
        // UCD_PATH(__ucd_devnum) directory does not exist
        if (mkdir(UCD_PATH(__ucd_devnum), 0777) != 0) {
            LOG_E("Error creating %s: %s", UCD_PATH(__ucd_devnum), strerror(errno));
            close(fd);
            return -1;
        } else {
            LOG_D("Created %s", UCD_PATH(__ucd_devnum));
        }
    } else {
        // UCD_PATH(__ucd_devnum) directory exists
        LOG_D("Recreating directory %s", UCD_PATH(__ucd_devnum));
        if (rmdir(UCD_PATH(__ucd_devnum)) != 0) {
            LOG_E("Error removing %s: %s", UCD_PATH(__ucd_devnum), strerror(errno));
            close(fd);
            return -1;
        } else {
            LOG_D("Removed %s", UCD_PATH(__ucd_devnum));
        }
        if (mkdir(UCD_PATH(__ucd_devnum), 0777) != 0) {
            LOG_E("Error creating %s: %s", UCD_PATH(__ucd_devnum), strerror(errno));
            close(fd);
            return -1;
        }
    }

    if (-1 == ucd_create_files()) {
        return -1;
    }

    __ucd_fd = fd;
    int rv = pthread_create(&__ucd_loop_thread, NULL, ucd_loop, (void*) &__ucd_fd);

    return 0;
}

int ucd_driver_attach(const char* pcDev, int devnum) {
    __ucd_devnum = devnum;
    int fd = open(pcDev, O_RDWR | O_NOCTTY | O_SYNC | O_EXCL);
    if (fd < 0) {
        LOG_E("Error %d opening %s: %s", errno, pcDev, strerror (errno));
        return -1;
    }

    if (!isatty(fd)) {
        LOG_E("%s is not a tty", pcDev);
        close(fd);
        return -1;
    } else {
        LOG_D("%s is a tty", pcDev);
    }

    struct termios config;
    if (tcgetattr(fd, &config) < 0) {
        LOG_E("Error getting termios attributes: %s", strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&config);
    cfsetispeed(&config, B115200);
    cfsetospeed(&config, B115200);
    //config.c_cflag |= CRTSCTS;

    // ///<Print all termios attributes in binary for debugging>
    // LOG_V("Termios attributes (binary): ");
    // LOG_V_TBC("c_iflag: "); uhfman_debug_print_bits(&config.c_iflag, sizeof(config.c_iflag)); LOG_V_CFIN("");
    // LOG_V_TBC("c_oflag: "); uhfman_debug_print_bits(&config.c_oflag, sizeof(config.c_oflag)); LOG_V_CFIN("");
    // LOG_V_TBC("c_cflag: "); uhfman_debug_print_bits(&config.c_cflag, sizeof(config.c_cflag)); LOG_V_CFIN("");
    // LOG_V_TBC("c_lflag: "); uhfman_debug_print_bits(&config.c_lflag, sizeof(config.c_lflag)); LOG_V_CFIN("");
    // LOG_V_TBC("c_line: "); uhfman_debug_print_bits(&config.c_line, sizeof(config.c_line)); LOG_V_CFIN("");
    // LOG_V("c_cc: ");
    // for (int i = 0; i < NCCS; i++) {
    //     LOG_V_TBC("c_cc[%d]: ", i); uhfman_debug_print_bits(&config.c_cc[i], sizeof(config.c_cc[i])); LOG_V_CFIN("");
    // }
    // LOG_V_TBC("c_ispeed: "); uhfman_debug_print_bits(&config.c_ispeed, sizeof(config.c_ispeed)); LOG_V_CFIN("");
    // LOG_V_TBC("c_ospeed: "); uhfman_debug_print_bits(&config.c_ospeed, sizeof(config.c_ospeed)); LOG_V_CFIN("");
    // ///</Print all termios attributes for debugging>

    if (tcsetattr(fd, TCSANOW, &config) < 0) {
        LOG_E("Error setting termios attributes: %s", strerror(errno));
        close(fd);
        return -1;
    } else {
        LOG_D("Termios attributes set successfully");
    }

    tcflush(fd, TCIOFLUSH); // Flush just in case there is any garbage in the buffers

    if (-1 == ucd_init(fd)) {
        return -1;
    }

    return fd;
}


int ucd_driver_detach(int fd) {
    // Cancel the UCD loop
    int rv = pthread_cancel(__ucd_loop_thread);
    if (rv != pthread_cancel(__ucd_loop_thread)) {
        LOG_E("Error cancelling UCD loop thread: %s (error code: %d)", strerror(rv), rv);
        return -1;
    }

    // Delete the control and data files
    if (-1 == ucd_delete_files()) {
        return -1;
    }
    // Delete the UCD_PATH(__ucd_devnum) directory  if it exists
    if (access(UCD_PATH(__ucd_devnum), F_OK) == 0) {
        if (-1 == rmdir(UCD_PATH(__ucd_devnum))) {
            LOG_E("Error removing %s: %s", UCD_PATH(__ucd_devnum), strerror(errno));
            return -1;
        } else {
            LOG_D("Removed %s", UCD_PATH(__ucd_devnum));
        }
    }
    return close(fd);
}