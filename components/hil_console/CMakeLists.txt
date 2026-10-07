# Optional hardware-in-the-loop console (CONFIG_HIL_CONSOLE). Without it the
# component compiles nothing and adds nothing to the firmware.
set(srcs)
if(CONFIG_HIL_CONSOLE)
    set(srcs "hil_console.c" "hil_json.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES console esp_driver_usb_serial_jtag vfs
)
