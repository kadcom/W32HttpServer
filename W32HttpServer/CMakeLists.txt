cmake_minimum_required(VERSION 3.14)

project(w32http LANGUAGES C RC)

set(SOURCES logging.c main_window.c main_window_handlers.c server_win32.c server_http.c)

add_executable(w32http WIN32 ${SOURCES} w32httpd.rc)
set_property(TARGET w32http PROPERTY
  WATCOM_RUNTIME_LIBRARY "MultiThreaded")
target_link_libraries(w32http ws2_32 comctl32)
