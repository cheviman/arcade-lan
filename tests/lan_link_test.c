/**
 *    Name:        lan_link_test.c
 *    Description: the library's test of a linked game (LAN_LINK): four devices in one program over 127.0.0.1, a
 *                 tenth of their datagrams lost
 *    Version:     0.2.0
 *    Created:     06-11-2026
 *    Modified:    11-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Each device publishes a block each frame: its name's first letter and the frames it has run. Checked: the rooms
 *    (the beacon, JOIN by it and by the address), what a device says of itself, the game's devices and every one's
 *    newest block on every other, a device that joins the room while the game goes on (not in it, in the next one),
 *    a device unheard (waited for, heard again), a device gone (out of the game and of the room; heard again, back in
 *    the room only), a device's LEAVE, the host's LEAVE (the next one hosts the room on, START again from it, and is
 *    found by the SEEK of a device that hears no beacon), and a host unheard for 3 seconds.
 */
#include "arcade_lan.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEVICES             4
#define BEACON_PORT         27751      /* (its seeks' port the next, the library's choice) */
#define DEAF_BEACON_PORT    27753      /* where a device listens that hears none of the room's beacons */
#define CLOSE               30         /* frames: a block this near the device's own count is fresh */

typedef struct
{
	const char *name;
	int players;
	lan_device *lan;
	bool started;
	uint32_t frames;           /* run since its game started */
	bool off;                  /* not ticked: unheard */
} device;

static device devices[DEVICES] = { { "HOST", 1 }, { "SEARCHER", 2 }, { "ADDRESS", 1 }, { "LATE", 1 } };
static int failures;

static void check(bool ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

static void tick(device *each)
{
	if (each->off)
		return;
	lan_update(each->lan);
	if (lan_game_started(each->lan, NULL, NULL, NULL))
	{
		each->started = true;
		each->frames = 0;
	}
	if (lan_role_of(each->lan) == LAN_PLAYING)
	{
		uint8_t block[5];

		each->frames++;
		block[0] = (uint8_t)each->name[0];
		for (int at = 0; at < 4; at++)
			block[1 + at] = (uint8_t)(each->frames >> (8 * at));
		lan_publish(each->lan, block, sizeof(block));
	}
}

static void tick_all(int ticks)
{
	for (int count = 0; count < ticks; count++)
		for (int index = 0; index < DEVICES; index++)
			tick(&devices[index]);
}

static device *by_name(const char *name)
{
	for (int index = 0; index < DEVICES; index++)
		if (strcmp(devices[index].name, name) == 0)
			return &devices[index];
	return NULL;
}

/* On 'each', every other device of the game not gone: its newest block its own, and fresh */
static bool blocks_fresh(device *each)
{
	lan_member members[LAN_DEVICES];
	const int count = lan_game_members(each->lan, members);

	for (int index = 0; index < count; index++)
	{
		uint8_t block[LAN_BLOCK_MOST];
		const size_t size = lan_block(each->lan, index, block);
		const device *other = by_name(members[index].name);
		uint32_t frames = 0;

		if (members[index].own || members[index].gone)
			continue;
		if (size != 5 || !other || block[0] != (uint8_t)other->name[0])
		{
			printf("      at %s: %s's block %zu bytes\n", each->name, members[index].name, size);
			return false;
		}
		for (int at = 0; at < 4; at++)
			frames |= (uint32_t)block[1 + at] << (8 * at);
		if (other->frames - frames > CLOSE)
		{
			printf("      at %s: %s's block of frame %" PRIu32 ", its own %" PRIu32 "\n", each->name, other->name, frames,
				other->frames);
			return false;
		}
	}
	return count > 0;
}

/* The game's devices on 'each', by their names in its order: "HOST SEARCHER ADDRESS" */
static void game_names(device *each, char *text, size_t size)
{
	lan_member members[LAN_DEVICES];
	const int count = lan_game_members(each->lan, members);

	text[0] = '\0';
	for (int index = 0; index < count; index++)
		snprintf(text + strlen(text), size - strlen(text), "%s%s", index ? " " : "", members[index].name);
}

/* The member 'name' of 'each''s room list, or of its game (game true) */
static bool member_flags(device *each, const char *name, bool game, lan_member *found)
{
	lan_member members[LAN_DEVICES];
	const int count = game ? lan_game_members(each->lan, members) : lan_members(each->lan, members);

	for (int index = 0; index < count; index++)
		if (strcmp(members[index].name, name) == 0)
		{
			*found = members[index];
			return true;
		}
	return false;
}

int main(void)
{
	lan_config config = { { 'L', 'I', 'N', 'K' }, 1, BEACON_PORT, 0, 0, 10, LAN_LINK };
	device *host = &devices[0], *searcher = &devices[1], *address = &devices[2], *late = &devices[3];
	lan_member members[LAN_DEVICES], one;
	lan_room rooms[LAN_ROOMS];
	char text[128], name[LAN_NAME_LENGTH + 1];
	const char *problem;
	int found = 0, ticks;
	bool waited = false;

	for (int index = 0; index < DEVICES; index++)
	{
		devices[index].lan = lan_open(&config, devices[index].name, devices[index].players);
		if (!devices[index].lan)
		{
			printf("FAIL  no network for %s\n", devices[index].name);
			return 1;
		}
	}

	/* the room */
	check(lan_host(host->lan), "the host's room");
	lan_search(searcher->lan);
	{
		const lan_address at = { 0x7F000001u, lan_port(host->lan) };
		const lan_room by_address = lan_room_at(at);

		check(lan_join(address->lan, &by_address), "JOIN by the host's address");
	}
	for (ticks = 0; ticks < 200 && !found; ticks++)
	{
		tick_all(1);
		found = lan_rooms(searcher->lan, rooms);
	}
	check(found == 1 && strcmp(rooms[0].name, "HOST") == 0, "the room found");
	if (found)
		lan_join(searcher->lan, &rooms[0]);
	for (ticks = 0; ticks < 400 && lan_members(host->lan, members) != 3; ticks++)
		tick_all(1);
	tick_all(40);
	check(lan_members(host->lan, members) == 3 && lan_role_of(address->lan) == LAN_IN_ROOM
		&& lan_role_of(searcher->lan) == LAN_IN_ROOM, "three devices in the room");

	/* what a device says of itself */
	lan_set_status(searcher->lan, 5);
	tick_all(40);
	check(member_flags(host, "SEARCHER", false, &one) && one.status == 5, "the host hears what a device says of itself");
	check(member_flags(address, "SEARCHER", false, &one) && one.status == 5, "and the room's list tells the others");
	lan_set_status(searcher->lan, 0);

	/* the game */
	check(lan_start(host->lan, NULL, 0), "the host's START");
	tick_all(300);
	check(host->started && searcher->started && address->started, "the game started on every device");
	game_names(address, text, sizeof(text));
	check(strcmp(text, "HOST ADDRESS SEARCHER") == 0 || strcmp(text, "HOST SEARCHER ADDRESS") == 0,
		"the game's devices in the room's order, the host first");
	printf("      the game: %s\n", text);
	check(blocks_fresh(host) && blocks_fresh(searcher) && blocks_fresh(address),
		"every device has every other's newest block, a tenth of the datagrams lost");

	/* a device joins the room while the game goes on */
	{
		const lan_address at = { 0x7F000001u, lan_port(host->lan) };
		const lan_room by_address = lan_room_at(at);

		lan_join(late->lan, &by_address);
	}
	tick_all(120);
	check(lan_members(host->lan, members) == 4 && lan_role_of(late->lan) == LAN_IN_ROOM,
		"a device joins the room while the game goes on");
	check(lan_game_members(host->lan, members) == 3 && !member_flags(host, "LATE", true, &one), "not in the game");

	/* a device unheard for 2 seconds: waited for, then heard again */
	address->off = true;
	for (ticks = 0; ticks < 120; ticks++)
	{
		tick_all(1);
		waited = waited || (lan_waiting(host->lan, name) && strcmp(name, "ADDRESS") == 0);
	}
	check(waited, "a device unheard: waited for, by name");
	check(member_flags(searcher, "ADDRESS", true, &one) && !one.heard && !one.gone, "unheard, not gone");
	address->off = false;
	tick_all(60);
	check(member_flags(searcher, "ADDRESS", true, &one) && one.heard && blocks_fresh(searcher) && blocks_fresh(address),
		"heard again, its blocks fresh");

	/* a device gone: out of the game and of the room */
	address->off = true;
	tick_all(240);
	check(member_flags(host, "ADDRESS", true, &one) && one.gone && member_flags(searcher, "ADDRESS", true, &one)
		&& one.gone, "a device unheard for 3 seconds: gone from the game");
	check(!member_flags(host, "ADDRESS", false, &one) && lan_members(host->lan, members) == 3,
		"and off the room's list");
	check(blocks_fresh(host) && blocks_fresh(searcher), "the others' blocks fresh");
	address->off = false;
	tick_all(60);
	check(member_flags(host, "ADDRESS", false, &one) && member_flags(host, "ADDRESS", true, &one) && one.gone,
		"the device gone heard again: back in the room, still gone from the game");
	lan_leave(address->lan);
	tick_all(60);
	check(!member_flags(host, "ADDRESS", false, &one) && lan_role_of(address->lan) == LAN_OFF, "its LEAVE: off the room");

	/* the next game: the room's devices now */
	check(lan_start(host->lan, NULL, 0), "START again while the game goes on");
	late->started = false;
	tick_all(200);
	game_names(host, text, sizeof(text));
	check(late->started && strcmp(text, "HOST SEARCHER LATE") == 0, "the next game: the device that joined in it");
	check(blocks_fresh(host) && blocks_fresh(searcher) && blocks_fresh(late), "every block fresh");

	/* the host leaves: the next device of the list hosts the room on */
	lan_leave(host->lan);
	tick_all(60);
	check(lan_hosting(searcher->lan), "the host's LEAVE: the next device hosts the room");
	check(lan_members(late->lan, members) == 2 && strcmp(members[0].name, "SEARCHER") == 0 && members[0].heard,
		"its list at the others, its host heard");
	check(member_flags(late, "HOST", true, &one) && one.gone && blocks_fresh(searcher) && blocks_fresh(late),
		"the host gone from the game, the others' blocks fresh");
	check(lan_start(searcher->lan, NULL, 0), "START from the new host");
	late->started = false;
	tick_all(120);
	game_names(late, text, sizeof(text));
	check(late->started && strcmp(text, "SEARCHER LATE") == 0, "its game");

	/* the room hosted on, found by a device that hears no beacon: the next host answers its SEEK */
	{
		lan_config deaf = config;
		lan_device *seeker;

		deaf.beacon_port = DEAF_BEACON_PORT;
		deaf.seek_port = BEACON_PORT + 1;
		seeker = lan_open(&deaf, "Seeker", 1);
		lan_search(seeker);
		for (ticks = 0, found = 0; ticks < 600 && !found; ticks++)
		{
			tick_all(1);
			lan_update(seeker);
			found = lan_rooms(seeker, rooms);
		}
		check(found == 1 && strcmp(rooms[0].name, "SEARCHER") == 0 && rooms[0].devices == 2 && !rooms[0].playing,
			"the room hosted on found by a SEEK: it takes devices");
		lan_close(seeker);
	}

	/* a host unheard for 3 seconds */
	searcher->off = true;
	tick_all(240);
	check(lan_hosting(late->lan) && lan_members(late->lan, members) == 1, "a host unheard: the next one hosts on");
	problem = lan_problem(late->lan);
	check(problem == NULL, "with no problem said");

	for (int index = 0; index < DEVICES; index++)
		lan_close(devices[index].lan);
	printf(failures ? "%d failed\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
