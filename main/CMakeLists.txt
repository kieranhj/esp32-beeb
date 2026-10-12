# The disc embedded in the image and autobooted, from discs/:
#   idf.py -DBEEB_DISC=bs-twisted.ssd build
set(BEEB_DISC "crtc-tmoyos.ssd" CACHE STRING "disc image in discs/ to embed and autoboot")
string(MAKE_C_IDENTIFIER "${BEEB_DISC}" BEEB_DISC_SYM)

idf_component_register(
    SRCS
        "main.c"
        "machine_esp.c"
        "video_esp.c"
        "display.c"
        "bundle.c"
        "console.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_timer esp_lcd esp_driver_i2c esp_driver_spi esp_driver_gpio
        esp_partition nvs_flash esp_driver_usb_serial_jtag
    EMBED_FILES
        "../roms/mos3.20/mos.rom"
        "../roms/mos3.20/dfs.rom"
        "../roms/mos3.20/viewsht.rom"
        "../roms/mos3.20/edit.rom"
        "../roms/mos3.20/basic4.rom"
        "../roms/mos3.20/adfs.rom"
        "../roms/mos3.20/view.rom"
        "../roms/mos3.20/terminal.rom"
        "../discs/${BEEB_DISC}"
)

# The emulator's inner loops are integer code with no aliasing to worry about;
# -O2 is the baseline. Revisit per-file (-Os for cold code) once there is a
# measured reason: on the ESP32-S3 instruction-cache behaviour can beat raw -O2.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
# The video stream ring: 16 K words is 32 KB of internal SRAM and 0.4 of a
# frame of slack between the machine and the renderer.
target_compile_definitions(${COMPONENT_LIB} PRIVATE VS_RING=16384
    BEEB_DISC_SYM=${BEEB_DISC_SYM} BEEB_DISC_NAME="${BEEB_DISC}")
