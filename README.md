# arcade-lan

A game over the local network for the ports of decompiled arcade games: rooms of up to four devices, found by their
beacons, and their game played in one of two ways:

- **In lockstep through the room's host** (`LAN_LOCKSTEP`, TMNT's): every device runs the same frames with every
  player's inputs, as one cabinet would, and a game that is deterministic (the same start and the same inputs give the
  same frames, as a decompiled program is) stays the same on every device.
- **Linked** (`LAN_LINK`, OutRunners'): each device runs its own game, as one of the arcade's linked cabinets, and
  sends every other device a block of its own each frame (what the link board carried: its players' records); each
  one keeps every other's newest, and nothing waits frame by frame.

There is no game in it. A port gives its players' controls (or its block) each frame, and draws its own menus (HOST,
JOIN, the rooms, the room's list).

It is C11, with no library but the system's sockets: Winsock on Windows, POSIX on Linux, macOS, Android and iOS,
libnx's BSD sockets on the Switch. Every datagram is written byte by byte, so the devices need not be alike (a
computer, a phone, a Switch).

## How a port uses it

In lockstep:

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

Linked:

```c
lan_config config = { { 'O', 'R', 'U', 'N' }, 2, 27720, 0, 0, 0, LAN_LINK };
lan_device *device = lan_open(&config, "AYRTON", 2);

/* every frame of the port's: */
lan_set_status(device, in_a_race ? 1 : 0);    /* what the others see of it (lan_member's status) */
lan_update(device);
if (lan_game_started(device, NULL, NULL, NULL))
	;    /* linked: lan_game_members lists the game's devices, this one among them (own) */
if (lan_role_of(device) == LAN_PLAYING)
{
	lan_publish(device, my_block, my_block_size);
	for (int n = 0; n < lan_game_members(device, members); n++)
		if (!members[n].own && !members[n].gone)
			size = lan_block(device, n, block);    /* its newest (members[n].heard: not unheard a third of a second) */
}
```

The host's `lan_start(device, settings, size)` starts the game on every device of its room (linked, again while one
goes on: the room's devices then). `lan_members` gives the room's list (the host first; each device's players take
the slots in the list's order), `lan_problem` what went wrong ("NO NETWORK", "ROOM FULL", "ROOM PLAYING", "NO ANSWER",
"HOST GONE", "OUT OF STEP", "LEFT OUT").

### Where the system refuses broadcasts

iOS sends and receives UDP broadcasts only with Apple's multicast entitlement
(`com.apple.developer.networking.multicast`), which Apple grants on request. A port for iOS without it builds the
library with `ARCADE_LAN_NO_BROADCAST`, the library's CMake option (for Apple's systems alone: the others have their
broadcasts, and the library does not compile with it there):

```cmake
set(ARCADE_LAN_NO_BROADCAST ON)    # this port's device hears no broadcast and sends none
add_subdirectory(lan)
```

Nothing else changes in the port: `lan_host` and `lan_search` find the rooms by datagrams to each address of the
device's local networks (below). Those are some 250 datagrams a second while it hosts a room, so a linked game, whose
room stays open through its races, tells the library when to hold them:

```c
lan_hold_sweep(device, in_a_race);    /* every frame, beside lan_set_status; nothing in a library not built so */
```

In lockstep the library holds them itself through the game, whose room takes no one. The ports for the other systems
are built as ever, and need only be of a version of the library that has the seeks (0.3.0) for a device built so to
find their rooms. `lan_config`'s last member, `seek_port`, is the port those devices ask at: the one after
`beacon_port` unless the game gives another.

## How it works

- **Rooms.** A host sends its beacon to the network (255.255.255.255) and to 127.0.0.1 every half second, on the
  game's beacon port; a device looking for rooms listens there and forgets a room unheard for 3 seconds. A device
  asks to JOIN until the host's list has it, and again while in the room (with what it says of itself); the host sends
  its list every half second and when it changes, with where it hears each device. A device can also join by the
  host's address (`lan_room_at`), where no beacon comes.
- **Rooms without broadcasts.** A device looking for rooms also sends a SEEK every second, from the socket it hears
  the beacons at, to the game's seeks' port (the one after the beacons'), which every room's host listens at, on
  every system; the host answers with its beacon, to the address the SEEK came from, and the room shows in the search
  as one heard by its beacon. The SEEK goes to 127.0.0.1 (a room hosted on the same computer) and, in a library built
  with `ARCADE_LAN_NO_BROADCAST`, to every other address of the device's local networks, one by one (a sweep: the
  networks `getifaddrs` lists that are up and private, 10.x, 172.16-31.x or 192.168.x, and of one larger than a /24
  the /24 around the device). A room's host built so sends its beacon the same way once a second, to the beacons'
  port, so that the devices that only listen for beacons find it: not through a game in lockstep, nor while the port
  holds it (`lan_hold_sweep`). The beacon is the same as before and a SEEK is of no room, so the protocol's number
  did not change: a device with a library before the seeks plays with one after them, and only does not answer a
  SEEK (a device that hears no broadcast does not find the room it hosts, and its own room is found by its sweep).
  The sweep is Darwin's alone: most of its datagrams go to addresses no device has, which Linux keeps for 3 seconds
  each, counted against the socket they came from, and the first sweep leaves that socket unable to send (measured:
  238 of the 254 sent, and nothing more for 3 seconds).
- **The game in lockstep.** At the host's START every device begins at frame 0. The controls a device gives at a frame
  play `delay` frames later (the first `delay` frames have none), so that the datagrams have come by then: 2 or 3
  frames on a LAN. A device sends the host its players' inputs the host has not put together yet (INPUT); the host
  puts every slot's of a frame together as they come and sends each device the frames it has not had (FRAMES). A
  device runs a frame only once it has it whole, else it holds. Each datagram carries the last frames again, so a lost
  one costs nothing; a frame is whole only once every device has given its inputs for it, so no device is ever more
  than a few frames from the others.
- **The linked game.** The START lists the game's devices (the room's then); each one answers it, and sends its block
  to every other device of the game each time the port publishes one, straight to it (BLOCK): to where the room's list
  says the host hears it, until it hears that device itself, each one at the address first heard from it then. A lost
  block costs nothing, the next one comes a frame later. The room takes devices while the game goes on (in the next
  one), and the host's START starts another.
- **Unheard and gone.** A device unheard for a third of a second is waited for (in lockstep the frames hold, and the
  host says for whom; linked, the port decides); after 3 seconds it is gone for the rest of the game: in lockstep its
  slots' inputs are none from then on, linked its block is no longer taken, and the game goes on. The host gone ends a
  game in lockstep for every device; a linked room is hosted on by the next device of its list (each device takes the
  host off its list after 3 seconds unheard, or at its LEAVE).
- **Out of step** (lockstep). Each device hands the hash of its state after each frame; the devices send theirs to the
  host, which compares them with its own and tells every device when one differs.

## Its tests

Devices in one program over 127.0.0.1, with a tenth of their datagrams lost:

- `tests/lan_test.c` (lockstep): a host, a device that finds its room by its beacon, and one with two players that
  joins by the host's address. It checks the rooms and the slots, that every device runs the same frames, the frames
  held for a device unheard, the game going on without a device gone, OUT OF STEP and HOST GONE. Then a room found
  with no beacon heard: a host whose beacons go to a port the others do not listen at, found (and joined) by the
  device whose SEEK it hears and not by one that seeks at another port; and the addresses a sweep goes to.
- `tests/lan_link_test.c` (linked): four devices. It checks what a device says of itself, every device's newest block
  on every other, a device that joins while the game goes on (in the next one), a device unheard and heard again, a
  device gone (out of the game and of the room; heard again, back in the room only), a device's LEAVE, the host's LEAVE
  (the next one hosts the room on, starts the next game, and answers the SEEK of a device that hears no beacon) and a
  host unheard for 3 seconds.

```
cmake -S . -B build
cmake --build build --config Release
build/Release/lan_test
build/Release/lan_link_test
```

## License

BSD 3-Clause (LICENSE). The sockets' code comes from outrunners-port's network game, and the rooms found without
broadcasts from its iOS port (outrunners-port's pull request 14).
