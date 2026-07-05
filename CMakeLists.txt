cmake_minimum_required(VERSION 3.16)
project(PoolControllino LANGUAGES C CXX ASM)

include(cmake/lto.cmake)
include(cmake/arduino_core.cmake)
include(cmake/arduino_hex.cmake)
include(cmake/arduino_upload.cmake)
include(cmake/libraries.cmake/arduino_controllino.cmake)
include(cmake/libraries.cmake/ethernet.cmake)
include(cmake/libraries.cmake/simple_utility_library.cmake)
include(cmake/libraries.cmake/simple_webserver.cmake)
include(cmake/libraries.cmake/ntpclient.cmake)
include(cmake/libraries.cmake/arduino_json.cmake)
include(cmake/libraries.cmake/eeprom.cmake)
include(cmake/libraries.cmake/truerandom.cmake)
include(cmake/libraries.cmake/onewire.cmake)
include(cmake/libraries.cmake/dallastemperature.cmake)
include(cmake/libraries.cmake/arduino_rs485.cmake)
include(cmake/libraries.cmake/arduino_modbus_controllino.cmake)
include(cmake/libraries.cmake/spi.cmake)
include(cmake/libraries.cmake/mdns.cmake)
include(cmake/libraries.cmake/ethernet_webserver.cmake)
include(cmake/libraries.cmake/functional-vlpp.cmake)
include(cmake/libraries.cmake/arxcontainer.cmake)
include(cmake/libraries.cmake/acebutton.cmake)

add_executable(PoolControllino PoolControllino.cpp)
target_link_libraries(PoolControllino PUBLIC ArduinoCore)
target_link_libraries(PoolControllino PUBLIC ControllinoCore)
target_link_libraries(PoolControllino PUBLIC Ethernet)
target_link_libraries(PoolControllino PUBLIC SPI)
target_link_libraries(PoolControllino PUBLIC EEPROM)
target_link_libraries(PoolControllino PUBLIC MDNS)
target_link_libraries(PoolControllino PUBLIC EthernetWebserver)
target_link_libraries(PoolControllino PUBLIC TrueRandom)
target_link_libraries(PoolControllino PUBLIC ACEBUTTON)
target_link_libraries(PoolControllino PUBLIC OneWire)
target_link_libraries(PoolControllino PUBLIC DallasTemperature)

# Regenerate the git version header on every build (script mode), so the
# embedded version always matches the firmware being flashed. Exposed at
# runtime via GET /version and in the version.h gitVersion string.
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
add_custom_target(generate_version_header ALL
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DOUT_FILE=${GENERATED_DIR}/version_git.h
        -P ${CMAKE_SOURCE_DIR}/cmake/git_version.cmake
    BYPRODUCTS ${GENERATED_DIR}/version_git.h
    COMMENT "Generating git version header")
add_dependencies(PoolControllino generate_version_header)
target_include_directories(PoolControllino PUBLIC ${GENERATED_DIR})

target_compile_options(PoolControllino PUBLIC
    "-Wall"
    "-Wextra"
    "-pedantic"
    "-felide-constructors"
    "-std=c++0x"
    "-DCONTROLLINO_MEGA"
)
arduino_avr_hex(PoolControllino)

add_executable(blink blink.cpp)
target_link_libraries(blink PUBLIC ArduinoCore)
target_link_libraries(blink PUBLIC ControllinoCore)
target_link_libraries(blink PUBLIC Ethernet)
target_link_libraries(blink PUBLIC SPI)
target_link_libraries(blink PUBLIC EEPROM)
target_link_libraries(blink PUBLIC MDNS)
target_link_libraries(blink PUBLIC EthernetWebserver)
target_link_libraries(blink PUBLIC TrueRandom)

target_compile_options(blink PUBLIC
    "-Wall"
    "-Wextra"
    "-pedantic"
)
arduino_avr_hex(blink)

set(ARDUINO_PORT "/dev/ttyACM0"
    CACHE STRING "The serial port for uploading using avrdude")
arduino_avr_upload(blink ${ARDUINO_PORT})
arduino_avr_upload(PoolControllino ${ARDUINO_PORT})
