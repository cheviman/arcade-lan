/**
 *    Name:        arcade_lan.h
 *    Description: a game over the local network: rooms of up to four devices, found by their beacons, and their game
 *                 played in lockstep through the room's host (every device runs the same frames with every player's
 *                 inputs)
 *    Version:     0.1.0
 *    Created:     06-11-2026
 *    Modified:    06-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    No game in it: a game gives its players' controls as a few bytes a frame and runs its frames with the inputs the
 *    library hands back, which are every device's the same; the game's own state, the same on every device from the
 *    same start and the same inputs (a deterministic program), tells by its hash a device gone out of step.
 *
 *    A port's loop, each frame of its own:
 *        lan_update(device);
 *        in a room: its menus (lan_members; the host's lan_start)
 *        lan_game_started(device, ...): the game started from its settings, this device's players from first_slot
 *        in the game: lan_give_inputs(device, its players' controls);
 *                     if (lan_next_frame(device, inputs)) run a frame with them, lan_frame_hash(device, frame, hash);
 *                     else show the last picture again (it holds: lan_waiting says for whom)
 */
#ifndef ARCADE_LAN_H
#define ARCADE_LAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAN_DEVICES         4      /* a room's devices, at most */
#define LAN_PLAYERS         4      /* a room's players (its slots), at most */
#define LAN_INPUT_BYTES     4      /* a player's input at a frame, at most */
#define LAN_NAME_LENGTH     10     /* a device's name: A-Z, 0-9, '.', '-' and spaces */
#define LAN_ROOMS           8      /* the rooms a search keeps */
#define LAN_SETTINGS_MOST   256    /* the game's settings a START carries, at most */
#define LAN_DELAY_MOST      8

/* The game's: devices of another game or version are not heard */
typedef struct
{
	char game[4];          /* its four letters ("TMNT") */
	uint8_t version;
	uint16_t beacon_port;  /* where the rooms' beacons go: every device of the game looks there */
	int input_bytes;       /* a player's input's bytes, 1 to LAN_INPUT_BYTES */
	int delay;             /* the frames between a device's input and the frame it plays at, 1 to LAN_DELAY_MOST
	                          (2 or 3 on a LAN: the datagrams have come by then) */
	int loss;              /* for tests: the percent of its datagrams this device loses as it sends them */
} lan_config;

typedef struct lan_device lan_device;

typedef enum
{
	LAN_OFF,               /* in no room */
	LAN_SEARCHING,         /* looking for rooms */
	LAN_JOINING,           /* asking a room's host */
	LAN_IN_ROOM,           /* in a room's list, its game not started */
	LAN_PLAYING            /* in its game */
} lan_role;

/* A device's address: its IPv4 address (a.b.c.d as a << 24 | b << 16 | c << 8 | d) and UDP port */
typedef struct
{
	uint32_t ip;
	uint16_t port;
} lan_address;

/* A room heard */
typedef struct
{
	uint32_t room;
	char name[LAN_NAME_LENGTH + 1];    /* its host's */
	int devices, players;
	bool playing;                      /* its game going on: it takes no one */
	lan_address host;
} lan_room;

/* A device of the room's list, in its order (the host first) */
typedef struct
{
	uint32_t id;
	char name[LAN_NAME_LENGTH + 1];
	int players;           /* its local players */
	int first_slot;        /* the slot its first player takes: the players take them in the list's order */
	bool heard;            /* in the last third of a second (this device always) */
	bool gone;             /* its game's slots' inputs none from then on */
	bool own;              /* this device */
} lan_member;

/* A device on the network, 'name' its player's (or its own) and 'players' its local players (1 to LAN_PLAYERS): NULL
   where it has no network */
lan_device *lan_open(const lan_config *config, const char *name, int players);
void lan_close(lan_device *device);

/* Each frame of the game's, once, whether it runs one or holds: the datagrams taken and sent */
void lan_update(lan_device *device);

lan_role lan_role_of(const lan_device *device);
bool lan_hosting(const lan_device *device);

/* What went wrong, once (NULL when nothing new): "NO NETWORK", "ROOM FULL", "ROOM PLAYING", "NO ANSWER", "HOST GONE",
   "OUT OF STEP" */
const char *lan_problem(lan_device *device);

/* The rooms: a room of its own, its beacons sent; the rooms on the network looked for; joining one; out of a room
   (or of its game) */
bool lan_host(lan_device *device);
void lan_search(lan_device *device);
int lan_rooms(const lan_device *device, lan_room rooms[LAN_ROOMS]);
bool lan_join(lan_device *device, const lan_room *room);
void lan_leave(lan_device *device);

/* The room's list */
int lan_members(const lan_device *device, lan_member members[LAN_DEVICES]);

/* The game: the host's START, with the game's settings (bytes of its own, sent as they are) */
bool lan_start(lan_device *device, const void *settings, size_t size);

/* True once, as the game starts on this device: its settings ('settings' at least LAN_SETTINGS_MOST bytes), and the
   slot this device's first player takes */
bool lan_game_started(lan_device *device, void *settings, size_t *size, int *first_slot);

/* This device's players' controls (players x input_bytes, its first player's first), which play 'delay' frames on:
   at each of its frames (taken when the frame they are for has none yet) */
void lan_give_inputs(lan_device *device, const uint8_t *inputs);

/* The next frame's inputs of every slot (LAN_PLAYERS x input_bytes; zeros for a slot no one plays, or gone): true when
   they have come and the frame is to run, false while it holds */
bool lan_next_frame(lan_device *device, uint8_t *inputs);

/* The game's state's hash after it ran 'frame' (counted from 0, as lan_next_frame hands them): the host compares them */
void lan_frame_hash(lan_device *device, uint32_t frame, uint64_t hash);

/* The frame the game runs next; whether it holds for a device unheard, and its name */
uint32_t lan_frame_number(const lan_device *device);
bool lan_waiting(const lan_device *device, char name[LAN_NAME_LENGTH + 1]);

/* The device's socket's port, and a room joined by its host's address (where no beacon comes: tests, some networks) */
uint16_t lan_port(const lan_device *device);
lan_room lan_room_at(lan_address host);

#ifdef __cplusplus
}
#endif

#endif
