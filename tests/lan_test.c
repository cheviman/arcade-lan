/**
 *    Name:        lan_test.c
 *    Description: the library's test: three devices in one program over 127.0.0.1 (a host, one that finds its room by
 *                 its beacon, one that joins it by its address, with two players), a game of its own, a tenth of the
 *                 datagrams lost; and a room found with no beacon heard, by a SEEK
 *    Version:     0.2.0
 *    Created:     06-11-2026
 *    Modified:    11-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The game: a number every device steps with every slot's inputs at each frame. Checked: the rooms (the beacon,
 *    JOIN by it and by the address, the list, the slots), the same frames on every device, the frames held while a
 *    device is not heard and for whom, a device gone after 3 seconds (the others go on without it), a device out of
 *    step (its hash), the host gone.
 *
 *    The rooms found where no broadcast is heard: a host whose beacons go to a port the devices looking for rooms do
 *    not listen at (what a device hears of them where the system refuses broadcasts), found by the device whose SEEK
 *    it hears and not by the one that seeks at another port; and the addresses a sweep goes to (lan_udp.h's).
 */
#include "arcade_lan.h"
#include "../src/lan_udp.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEVICES             3
#define HISTORY             8192       /* each device's numbers, by frame */
#define BEACON_PORT         27731      /* (its seeks' port the next, the library's choice) */
#define FAR_BEACON_PORT     27733      /* the room found by a SEEK: its beacons' port, */
#define DEAF_BEACON_PORT    27734      /* the one the device that looks for it listens at, */
#define SEEK_PORT           27735      /* its seeks' port; */
#define OTHER_BEACON_PORT   27736      /* another device's, */
#define NOBODYS_SEEK_PORT   27738      /* and the one it seeks at, where no host listens */
#define INPUT_BYTES         2

typedef struct
{
	const char *name;
	int players;
	lan_device *lan;
	bool started;
	int first_slot;
	uint64_t number;
	uint32_t run;              /* the frames it ran */
	uint64_t history[HISTORY];
	uint32_t random;
	bool off;                  /* not ticked: unheard */
	bool wrong;                /* its hashes wrong from now on */
} device;

static device devices[DEVICES] = { { "HOST", 1 }, { "Searcher", 1 }, { "two-player", 2 } };
static int failures;

static void check(bool ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

static uint64_t step_game(uint64_t number, uint32_t frame, const uint8_t *inputs)
{
	for (int at = 0; at < LAN_PLAYERS * INPUT_BYTES; at++)
		number = (number ^ inputs[at]) * 0x100000001b3ull + (uint64_t)at;
	return number ^ frame;
}

static void tick(device *each)
{
	uint8_t settings[LAN_SETTINGS_MOST];
	size_t size;

	if (each->off)
		return;
	lan_update(each->lan);
	if (lan_game_started(each->lan, settings, &size, &each->first_slot))
	{
		each->started = true;
		each->number = size;
		for (size_t at = 0; at < size; at++)
			each->number = (each->number ^ settings[at]) * 0x100000001b3ull;
		each->run = 0;
	}
	if (lan_role_of(each->lan) != LAN_PLAYING)
		return;
	{
		uint8_t own[LAN_PLAYERS * INPUT_BYTES], all[LAN_PLAYERS * INPUT_BYTES];

		for (int at = 0; at < each->players * INPUT_BYTES; at++)
		{
			each->random = each->random * 1664525u + 1013904223u;
			own[at] = (uint8_t)(each->random >> 24);
		}
		lan_give_inputs(each->lan, own);
		if (lan_next_frame(each->lan, all))
		{
			const uint32_t frame = lan_frame_number(each->lan) - 1u;

			each->number = step_game(each->number, frame, all);
			each->history[frame % HISTORY] = each->number;
			each->run = frame + 1u;
			lan_frame_hash(each->lan, frame, each->number ^ (each->wrong ? 1u : 0u));
		}
	}
}

static void tick_all(int ticks)
{
	for (int count = 0; count < ticks; count++)
		for (int index = 0; index < DEVICES; index++)
			tick(&devices[index]);
}

static uint32_t least_run(void)
{
	uint32_t least = UINT32_MAX;

	for (int index = 0; index < DEVICES; index++)
		if (!devices[index].off && devices[index].run < least)
			least = devices[index].run;
	return least;
}

/* The devices ticked agree on every frame of the last 'frames' they all ran */
static bool same_frames(uint32_t frames)
{
	const uint32_t last = least_run();

	for (uint32_t frame = last > frames ? last - frames : 0; frame < last; frame++)
		for (int index = 1; index < DEVICES; index++)
			if (!devices[index].off && !devices[0].off
				&& devices[index].history[frame % HISTORY] != devices[0].history[frame % HISTORY])
			{
				printf("      frame %" PRIu32 ": %s %016" PRIx64 ", %s %016" PRIx64 "\n", frame, devices[0].name,
					devices[0].history[frame % HISTORY], devices[index].name, devices[index].history[frame % HISTORY]);
				return false;
			}
	return true;
}

/* A room found with no beacon heard, by a SEEK, and the addresses a sweep goes to */
static void rooms_by_seek(void)
{
	const lan_config hosting = { { 'T', 'E', 'S', 'T' }, 1, FAR_BEACON_PORT, INPUT_BYTES, 3, 10, LAN_LOCKSTEP,
		SEEK_PORT };
	const lan_config seeking = { { 'T', 'E', 'S', 'T' }, 1, DEAF_BEACON_PORT, INPUT_BYTES, 3, 10, LAN_LOCKSTEP,
		SEEK_PORT };
	const lan_config apart = { { 'T', 'E', 'S', 'T' }, 1, OTHER_BEACON_PORT, INPUT_BYTES, 3, 10, LAN_LOCKSTEP,
		NOBODYS_SEEK_PORT };
	lan_device *host = lan_open(&hosting, "Far host", 1), *seeker = lan_open(&seeking, "Seeker", 1);
	lan_device *other = lan_open(&apart, "Other", 1);
	lan_room rooms[LAN_ROOMS], none[LAN_ROOMS];
	lan_member members[LAN_DEVICES];
	uint32_t first = 0, last = 0;
	int found = 0, others = 0, ticks;

	if (!host || !seeker || !other)
	{
		check(false, "the devices of the room found by a SEEK");
		return;
	}
	check(lan_host(host), "a host whose beacons the others do not hear");
	lan_search(seeker);
	lan_search(other);
	for (ticks = 0; ticks < 600; ticks++)
	{
		lan_update(host);
		lan_update(seeker);
		lan_update(other);
		if (!found)
			found = lan_rooms(seeker, rooms);
		others += lan_rooms(other, none);
	}
	check(found == 1 && strcmp(rooms[0].name, "FAR HOST") == 0, "its room found by a SEEK, no beacon heard");
	check(found == 1 && rooms[0].host.ip == 0x7F000001u && rooms[0].host.port == lan_port(host) && !rooms[0].playing,
		"the room's host where its answer came from: its own socket");
	check(lan_rooms(seeker, rooms) == 1, "the room kept while its host answers");
	check(others == 0, "and no room for the device that seeks where no host listens");
	if (found)
		lan_join(seeker, &rooms[0]);
	for (ticks = 0; ticks < 400 && (lan_role_of(seeker) != LAN_IN_ROOM || lan_members(host, members) != 2); ticks++)
	{
		lan_update(host);
		lan_update(seeker);
	}
	check(lan_role_of(seeker) == LAN_IN_ROOM && lan_members(host, members) == 2, "JOIN, the room found so");
	lan_close(other);
	lan_close(seeker);
	lan_close(host);

	/* a sweep's addresses: 192.168.1.42/24, 10.1.2.42/8, 172.31.0.5/30, and those it has none for */
	check(lan_udp_sweep_range(0xC0A8012Au, 0xFFFFFF00u, &first, &last) && first == 0xC0A80101u && last == 0xC0A801FEu,
		"a sweep: a /24's addresses, but the network's own and its broadcast's");
	check(lan_udp_sweep_range(0x0A01022Au, 0xFF000000u, &first, &last) && first == 0x0A010201u && last == 0x0A0102FEu,
		"of a larger network, the /24 around the device");
	check(lan_udp_sweep_range(0xAC1F0005u, 0xFFFFFFFCu, &first, &last) && first == 0xAC1F0005u && last == 0xAC1F0006u,
		"of a smaller one, its own");
	check(!lan_udp_sweep_range(0x08080808u, 0xFFFFFF00u, &first, &last)
		&& !lan_udp_sweep_range(0xAC200001u, 0xFFFFFF00u, &first, &last)
		&& !lan_udp_sweep_range(0xA9FE0001u, 0xFFFF0000u, &first, &last)
		&& !lan_udp_sweep_range(0xC0A80101u, 0xFFFFFFFFu, &first, &last),
		"none of a network that is not private, nor of an address alone");
}

int main(void)
{
	lan_config config = { { 'T', 'E', 'S', 'T' }, 1, BEACON_PORT, INPUT_BYTES, 3, 10 };
	device *host = &devices[0], *searcher = &devices[1], *pair = &devices[2];
	lan_member members[LAN_DEVICES];
	lan_room rooms[LAN_ROOMS];
	const char *problem;
	char name[LAN_NAME_LENGTH + 1];
	int found = 0, count, ticks;
	uint32_t before;
	bool waited = false;

	for (int index = 0; index < DEVICES; index++)
	{
		devices[index].lan = lan_open(&config, devices[index].name, devices[index].players);
		devices[index].random = 0x9E3779B9u * (uint32_t)(index + 1);
		if (!devices[index].lan)
		{
			printf("FAIL  no network for %s\n", devices[index].name);
			return 1;
		}
	}

	/* the rooms */
	check(lan_host(host->lan), "the host's room");
	lan_search(searcher->lan);
	{
		const lan_address address = { 0x7F000001u, lan_port(host->lan) };
		const lan_room by_address = lan_room_at(address);

		check(lan_join(pair->lan, &by_address), "JOIN by the host's address");
	}
	for (ticks = 0; ticks < 200 && !found; ticks++)
	{
		tick_all(1);
		found = lan_rooms(searcher->lan, rooms);
	}
	check(found == 1 && strcmp(rooms[0].name, "HOST") == 0, "the room found, its host's name");
	if (found)
		lan_join(searcher->lan, &rooms[0]);
	for (ticks = 0; ticks < 400 && (lan_role_of(searcher->lan) != LAN_IN_ROOM || lan_role_of(pair->lan) != LAN_IN_ROOM
		|| lan_members(host->lan, members) != 3); ticks++)
		tick_all(1);
	tick_all(40);    /* (the list sent again to both) */
	count = lan_members(pair->lan, members);
	check(count == 3 && members[0].own == false && strcmp(members[0].name, "HOST") == 0, "the list at a device: the host first");
	check(lan_members(host->lan, members) == 3, "the host's list: three devices");
	printf("      the list:");
	for (int index = 0; index < count; index++)
		printf(" %s (%d player%s, slot %d)", members[index].name, members[index].players,
			members[index].players > 1 ? "s" : "", members[index].first_slot + 1);
	printf("\n");
	check(strcmp(members[2].name, "TWO-PLAYER") == 0 || strcmp(members[1].name, "TWO-PLAYER") == 0,
		"names kept as the library writes them (upper case)");

	/* the game */
	check(lan_start(host->lan, "SETTINGS", 8), "the host's START");
	tick_all(1500);
	check(host->started && searcher->started && pair->started, "the game started on every device");
	check(host->first_slot == 0 && ((pair->first_slot == 1 && searcher->first_slot == 3)
		|| (searcher->first_slot == 1 && pair->first_slot == 2)), "the slots: the host's first, then in the list's order");
	check(least_run() > 1400, "frames run with a tenth of the datagrams lost");
	printf("      frames run: %" PRIu32 " %" PRIu32 " %" PRIu32 " in 1500\n", host->run, searcher->run, pair->run);
	check(same_frames(1400), "every device the same frames");

	/* a device unheard for 2 seconds: the others hold, waiting for it, then go on with it */
	pair->off = true;
	before = least_run();
	for (ticks = 0; ticks < 120; ticks++)
	{
		tick_all(1);
		waited = waited || (lan_waiting(host->lan, name) && strcmp(name, "TWO-PLAYER") == 0);
	}
	check(host->run - before <= (uint32_t)config.delay + 2u && searcher->run - before <= (uint32_t)config.delay + 2u,
		"the frames held while a device is unheard");
	check(waited, "and the host says for whom");
	pair->off = false;
	tick_all(600);
	check(least_run() > before + 400, "the game going on when it is heard again");
	check(same_frames(500), "every device the same frames again");

	/* a device gone: after 3 seconds the others go on without it */
	pair->off = true;
	before = host->run;
	tick_all(800);
	check(host->run > before + 400 && searcher->run > before + 400, "the game going on without a device gone");
	check(same_frames(400), "the two left the same frames");
	count = lan_members(searcher->lan, members);
	check(count == 3 && members[pair->first_slot == 1 ? 1 : 2].gone, "the device gone, in the list");

	/* out of step */
	searcher->wrong = true;
	for (ticks = 0, problem = NULL; ticks < 200 && !problem; ticks++)
	{
		tick_all(1);
		problem = lan_problem(host->lan);
	}
	check(problem && strcmp(problem, "OUT OF STEP") == 0, "a device's hash that differs: OUT OF STEP at the host");
	for (ticks = 0, problem = NULL; ticks < 200 && !problem; ticks++)
	{
		tick_all(1);
		problem = lan_problem(searcher->lan);
	}
	check(problem && strcmp(problem, "OUT OF STEP") == 0, "and at the device");

	/* the host gone */
	lan_leave(host->lan);
	for (ticks = 0, problem = NULL; ticks < 300 && !problem; ticks++)
	{
		tick_all(1);
		problem = lan_problem(searcher->lan);
	}
	check(problem && strcmp(problem, "HOST GONE") == 0 && lan_role_of(searcher->lan) == LAN_OFF, "the host gone: HOST GONE");

	for (int index = 0; index < DEVICES; index++)
		lan_close(devices[index].lan);

	rooms_by_seek();
	printf(failures ? "%d failed\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
