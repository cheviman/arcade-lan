/**
 *    Name:        lan_udp.h
 *    Description: the library's sockets (lan_udp.c): UDP, non-blocking, IPv4
 *    Version:     0.1.0
 *    Created:     06-11-2026
 *    Modified:    06-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 */
#ifndef ARCADE_LAN_UDP_H
#define ARCADE_LAN_UDP_H

#include "arcade_lan.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_BROADCAST     0xFFFFFFFFu    /* 255.255.255.255: every device on the local network */
#define LAN_LOOPBACK      0x7F000001u    /* 127.0.0.1: this one (another game on the same computer) */

typedef intptr_t lan_socket;
#define LAN_NO_SOCKET     ((lan_socket)-1)

/* A UDP socket on 'port' (0: the system's choice), able to broadcast, non-blocking; 'shared' with the other games on
   the device (the beacons' port): LAN_NO_SOCKET where the device has no network */
lan_socket lan_udp_open(uint16_t port, bool shared);
void lan_udp_close(lan_socket socket);

/* The port it was given */
uint16_t lan_udp_port(lan_socket socket);

/* A datagram to 'to': false when it could not go */
bool lan_udp_send(lan_socket socket, lan_address to, const void *data, size_t size);

/* The next datagram that came, into 'data' (at most 'room' bytes), and who sent it: its size, 0 when none waits */
size_t lan_udp_receive(lan_socket socket, void *data, size_t room, lan_address *from);

#endif
