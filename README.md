How to prepare for using this software and how to interface with the abstraction provided by this driver
- Create tmpfs filesystem in RAM and mount it under path /custom :
`$ mount -t tmpfs -o size=16m tmpfs /custom`
- Set appropriate permissions for /custom
- The driver control files will be present inside the /custom/ucd driver directory which is created upon running the driver executable
- A serial port file has to be provided as an argument to the driver executable, for example `$ ucd /dev/ttyUSB0` will attach the driver to the serial port `/dev/ttyUSB0`

- Writing `f` to `/custom/ucd/request` will request a frame from an ESP32-CAM running the uart-cam firmware. To check if the frame is ready, you can read from `/custom/ucd/state`. `1` means ready and `0` means not ready.
- To perform an OTA update over UART, the driver user writes the new firmware to `/custom/ucd/firmware`, then triggers the update by writing `u` to `/custom/ucd/request`.
