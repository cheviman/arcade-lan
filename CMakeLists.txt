# arcade-lan: a game over the local network (rooms, and a game in lockstep through the room's host or linked), for
# the ports of the decompiled arcade games; a port adds it (add_subdirectory) and links arcade_lan
cmake_minimum_required(VERSION 3.16)
project(arcade_lan C)

add_library(arcade_lan STATIC src/lan.c src/lan_udp.c)
target_include_directories(arcade_lan PUBLIC include)
set_target_properties(arcade_lan PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(WIN32)
	target_link_libraries(arcade_lan PUBLIC ws2_32)
endif()

# Where the system refuses UDP broadcasts (iOS without Apple's multicast entitlement): the rooms found by datagrams to
# each address of the device's local networks. A port for an Apple system sets it before its add_subdirectory (the
# library does not compile with it for another: src/lan_udp.c)
option(ARCADE_LAN_NO_BROADCAST "Find the rooms without UDP broadcasts (iOS without the multicast entitlement)" OFF)
if(ARCADE_LAN_NO_BROADCAST)
	target_compile_definitions(arcade_lan PRIVATE ARCADE_LAN_NO_BROADCAST)
endif()
if(MSVC)
	target_compile_options(arcade_lan PRIVATE /W4)
	target_compile_definitions(arcade_lan PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
else()
	target_compile_options(arcade_lan PRIVATE -Wall -Wextra)
endif()

# The library's own tests (devices over 127.0.0.1: lockstep, linked): only when it is the project built, not a
# port's part
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	enable_testing()
	foreach(test lan_test lan_link_test)
		add_executable(${test} tests/${test}.c)
		target_link_libraries(${test} PRIVATE arcade_lan)
		set_target_properties(${test} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
		add_test(NAME ${test} COMMAND ${test})
	endforeach()
endif()
