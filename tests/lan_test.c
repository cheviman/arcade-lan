/**
 *    Name:        lan_test.c
 *    Description: the library's test: three devices in one program over 127.0.0.1 (a host, one that finds its room by
 *                 its beacon, one that joins it by its address, with two players), a game of its own, a tenth of the
 *                 datagrams lost
 *    Version:     0.1.0
 *    Created:     06-11-2026
 *    Modified:    06-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The game: a number every device steps with every slot's inputs at each frame. Checked: the rooms (the beacon,
 *    JOIN by it and by the address, the list, the slots), the same frames on every device, the frames held while a
 *    device is not heard and for whom, a device gone after 3 seconds (the others go on without it), a device out of
 *    step (its hash), the host gone.
 */
#include "arcade_lan.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEVICES             3
#define HISTORY             8192       /* each device's numbers, by frame */
#define BEACON_PORT         27731
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
	check(found == 1 && strcmp(rooms[0].name, "HOST") == 0, "the room found by its beacon, its host's name");
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
	printf(failures ? "%d failed\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
