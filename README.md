# arcade-lan

A game over the local network for the ports of decompiled arcade games: rooms of up to four devices, found by their
beacons, and their game played in **lockstep through the room's host**. Every device runs the same frames with every
player's inputs, as one cabinet would, and a game that is deterministic (the same start and the same inputs give the
same frames, as a decompiled program is) stays the same on every device.

There is no game in it. A port gives its players' controls as a few bytes a frame, runs its frames with the inputs the
library hands back, hands it the hash of its state after each frame, and draws its own menus (HOST, JOIN, the rooms,
the room's list).

It is C11, with no library but the system's sockets: Winsock on Windows, POSIX on Linux, macOS and Android, libnx's
BSD sockets on the Switch. Every datagram is written byte by byte, so the devices need not be alike (a computer, a
phone, a Switch).

## How a port uses it

```c
lan_config config = { { 'T', 'M', 'N', 'T' }, 1, 27740, 1, 3, 0 };    /* game, version, beacon port, input bytes, delay */
lan_device *device = lan_open(&config, "LEONARDO", 1);               /* this device's name, its local players */

/* HOST: */ lan_host(device);
/* JOIN: */ lan_search(device); ... lan_rooms(device, rooms); lan_join(device, &rooms[n]);

/* every frame of the port's, whether the game runs or holds: */
lan_update(device);
if (lan_game_started(device, settings, &size, &first_slot))
	;    /* the game starts from 'settings' (the host's), this device's players from 'first_slot' */
if (lan_role_of(device) == LAN_PLAYING)
{
	lan_give_inputs(device, my_players_controls);
	if (lan_next_frame(device, every_slots_inputs))
	{
		run_one_frame(every_slots_inputs);
		lan_frame_hash(device, lan_frame_number(device) - 1, state_hash());
	}
	else
		;    /* it holds: show the last picture again (lan_waiting says for whom) */
}
```

The host's `lan_start(device, settings, size)` starts the game on every device of its room. `lan_members` gives the
room's list (the host first; each device's players take the slots in the list's order), `lan_problem` what went wrong
("NO NETWORK", "ROOM FULL", "ROOM PLAYING", "NO ANSWER", "HOST GONE", "OUT OF STEP").

## How it works

- **Rooms.** A host sends its beacon to the network (255.255.255.255) and to 127.0.0.1 every half second, on the
  game's beacon port; a device looking for rooms listens there and forgets a room unheard for 3 seconds. A device
  asks to JOIN until the host's list has it; the host sends its list every half second and when it changes. A device
  can also join by the host's address (`lan_room_at`), where no beacon comes.
- **The game.** At the host's START every device begins at frame 0. The controls a device gives at a frame play
  `delay` frames later (the first `delay` frames have none), so that the datagrams have come by then: 2 or 3 frames
  on a LAN. A device sends the host its players' inputs the host has not put together yet (INPUT); the host puts every
  slot's of a frame together as they come and sends each device the frames it has not had (FRAMES). A device runs a
  frame only once it has it whole, else it holds. Each datagram carries the last frames again, so a lost one costs
  nothing; a frame is whole only once every device has given its inputs for it, so no device is ever more than a few
  frames from the others.
- **Unheard and gone.** A device unheard for a third of a second is waited for (the frames hold, and the host says
  for whom); after 3 seconds it is gone: its slots' inputs are none from then on, and the game goes on. The host gone
  ends the game for every device.
- **Out of step.** Each device hands the hash of its state after each frame; the devices send theirs to the host,
  which compares them with its own and tells every device when one differs.

## Its test

`tests/lan_test.c` runs three devices in one program over 127.0.0.1, with a tenth of their datagrams lost: a host, a
device that finds its room by its beacon, and one with two players that joins by the host's address. It checks the
rooms and the slots, that every device runs the same frames, the frames held for a device unheard, the game going on
without a device gone, OUT OF STEP and HOST GONE.

```
cmake -S . -B build
cmake --build build --config Release
build/Release/lan_test
```

## License

BSD 3-Clause (LICENSE). The sockets' code comes from outrunners-port's network game.
