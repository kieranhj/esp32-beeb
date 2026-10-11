idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
)

# The emulator's inner loops are integer code with no aliasing to worry about;
# -O2 is the baseline. Revisit per-file (-Os for cold code) once Phase 0 has
# numbers: on the ESP32-S3 instruction-cache behaviour can beat raw -O2.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
