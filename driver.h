/**
 * @brief Attach UCD driver to a serial port
 * @param pcDev Serial port device path
 * @param devnum Device number
 * @param shoudDrain 0 for no input draining, 1 for input draining
 * @return File descriptor of the serial port, or -1 if an error occurred
 */
int ucd_driver_attach(const char* pcDev, int devnum, int shoudDrain);

/**
 * @brief Detach UCD driver from a serial port
 * @param fd File descriptor of the serial port
 * @return 0 if successful, -1 if an error occurred
 */
int ucd_driver_detach(int fd);