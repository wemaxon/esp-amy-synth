cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main es8388 ads1115 pcf8574 pocket-oled)
project(esp-amy-synth)
