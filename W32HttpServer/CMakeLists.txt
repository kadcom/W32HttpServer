cmake_minimum_required(VERSION 3.14)

project(w32http LANGUAGES C RC)

set(SOURCES logging.c main_window.c main_window_handlers.c server_win32.c server_http.c platform_detect.c thread_pool.c)

add_executable(w32http WIN32 ${SOURCES} w32httpd.rc)
set_property(TARGET w32http PROPERTY
  WATCOM_RUNTIME_LIBRARY "MultiThreaded")
# ws2_32   - Winsock 2
# comctl32 - common controls
# shell32  - SHBrowseForFolder / SHGetPathFromIDList / SHGetMalloc (folder picker)
# ole32    - OleInitialize / OleUninitialize (needed for the new-style folder dialog)
target_link_libraries(w32http ws2_32 comctl32 shell32 ole32)
