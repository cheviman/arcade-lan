/**
 *    Name:        lan_udp.c
 *    Description: the library's sockets: UDP, non-blocking, on Windows (Winsock), the Switch (libnx's BSD sockets)
 *                 and the rest (POSIX: Linux, macOS, Android)
 *    Version:     0.1.0
 *    Created:     06-11-2026
 *    Modified:    06-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    outrunners-port's console/net_udp.c, each socket its caller's (several devices in one program: the tests).
 */
#include "lan_udp.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close_socket    closesocket
#else
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define close_socket    close
#endif

static bool started;

/* The system's sockets, once (Winsock's, libnx's) */
static bool start(void)
{
	if (started)
		return true;
#ifdef _WIN32
	{
		WSADATA data;

		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			return false;
	}
#elif defined(__SWITCH__)
	if (R_FAILED(socketInitializeDefault()))
		return false;
#endif
	started = true;
	return true;
}

lan_socket lan_udp_open(uint16_t port, bool shared)
{
	struct sockaddr_in address;
	const int on = 1;
#ifdef _WIN32
	SOCKET opened;
#else
	int opened;
#endif

	if (!start())
		return LAN_NO_SOCKET;
	opened = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
	if (opened == INVALID_SOCKET)
		return LAN_NO_SOCKET;
#else
	if (opened < 0)
		return LAN_NO_SOCKET;
#endif
	setsockopt(opened, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
	if (shared)
	{
		setsockopt(opened, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
#if defined(SO_REUSEPORT) && !defined(__SWITCH__)
		setsockopt(opened, SOL_SOCKET, SO_REUSEPORT, (const char *)&on, sizeof(on));    /* (macOS, Linux) */
#endif
	}
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(opened, (const struct sockaddr *)&address, sizeof(address)) != 0)
	{
		close_socket(opened);
		return LAN_NO_SOCKET;
	}
#ifdef _WIN32
	{
		u_long non_blocking = 1;

		ioctlsocket(opened, FIONBIO, &non_blocking);
	}
#else
	fcntl(opened, F_SETFL, fcntl(opened, F_GETFL, 0) | O_NONBLOCK);
#endif
	return (lan_socket)opened;
}

void lan_udp_close(lan_socket socket)
{
	if (socket != LAN_NO_SOCKET)
		close_socket(socket);
}

uint16_t lan_udp_port(lan_socket socket)
{
	struct sockaddr_in address;
	socklen_t size = sizeof(address);

	if (socket == LAN_NO_SOCKET || getsockname(socket, (struct sockaddr *)&address, &size) != 0)
		return 0;
	return ntohs(address.sin_port);
}

bool lan_udp_send(lan_socket socket, lan_address to, const void *data, size_t size)
{
	struct sockaddr_in address;

	if (socket == LAN_NO_SOCKET)
		return false;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(to.port);
	address.sin_addr.s_addr = htonl(to.ip);
	return sendto(socket, (const char *)data, (int)size, 0, (const struct sockaddr *)&address, sizeof(address))
		== (int)size;
}

size_t lan_udp_receive(lan_socket socket, void *data, size_t room, lan_address *from)
{
	if (socket == LAN_NO_SOCKET)
		return 0;
	for (;;)
	{
		struct sockaddr_in sender;
		socklen_t sender_size = sizeof(sender);
		const int got = (int)recvfrom(socket, (char *)data, (int)room, 0, (struct sockaddr *)&sender, &sender_size);

		if (got > 0)
		{
			from->ip = ntohl(sender.sin_addr.s_addr);
			from->port = ntohs(sender.sin_port);
			return (size_t)got;
		}
#ifdef _WIN32
		/* (a datagram sent earlier to a port no longer open, or one too large: the next) */
		if (got < 0 && (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAEMSGSIZE))
			continue;
#else
		if (got < 0 && errno == EINTR)
			continue;
#endif
		return 0;
	}
}
