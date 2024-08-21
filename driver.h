/**
 * @brief Attach UCD driver to a serial port
 * @param pcDev Serial port device path
 * @return File descriptor of the serial port, or -1 if an error occurred
 */
int ucd_driver_attach(const char* pcDev);

/**
 * @brief Detach UCD driver from a serial port
 * @param fd File descriptor of the serial port
 * @return 0 if successful, -1 if an error occurred
 */
int ucd_driver_detach(int fd);