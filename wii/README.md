# Nookery for the Wii

Animal Crossing (GameCube) running natively on the Wii from the Homebrew Channel, off your own disc
image, with online play: host your town, or travel to someone else's.

Unofficial: not made by, affiliated with or endorsed by Nintendo. No game data is included.

- [Install](#install) · [Files](#files-on-the-sd-card) · [Controls](#controls) · [Settings](#settings) ·
  [Online play](#online-play) · [Troubleshooting](#troubleshooting)
- [Building](#building) · [Testing](#testing) · [Source layout](#source-layout) · [Credits](#credits)

## Install

You need a Wii with the [Homebrew Channel](https://wiibrew.org/wiki/Homebrew_Channel), an SD card, and
your own disc image of Animal Crossing for the GameCube (USA, `GAFE01`, revision 0).

1. Unzip a release (`nookery-wii-vX.Y.Z.zip`, from the
   [Releases](https://github.com/wize-logic/Nookery-Wii/releases) page or [built](#building) here) onto
   the SD card's root. The Wii uses `apps/nookery/`; `nookery-lobby-server/` is for whoever runs a
   [lobby server](server/README.md).
2. Copy your disc image to `apps/nookery/game.iso` (`.iso`, `.gcm` or `.ciso`; not `.rvz`, which Dolphin
   converts to `.iso`).
3. Start Nookery from the Homebrew Channel and choose **Play**.

To carry on with a town from Dolphin or the PC port, copy its `.gci` files into `apps/nookery/card_a/`.
Saves made on the Wii work there too.

## Files on the SD card

Everything is in `sd:/apps/nookery/`. Only the disc image may be on a USB drive instead.

| Path | Contents |
|---|---|
| `game.iso` | Your disc image. Also found as `game.gcm` or `game.ciso`, as `GAFE01.*` in `/games`, or under any name in `apps/nookery`, `/games` or the root of the SD card or a USB drive |
| `card_a/` | Memory card slot A: your town, as `.gci` files named the way Dolphin names them |
| `card_b/` | Slot B: towns to visit by train |
| `nes_roms/` | Your own `.nes` ROMs, which the game's NES consoles offer as cartridges |
| `settings.ini` | [Settings](#settings), made at the first start |
| `lobby_servers.txt` | [Lobby servers](#lobby-servers), one `host[:port]` per line, eight at most |
| `net/town/` | The host's town while you are a guest online |
| `logs/log.txt` | The last run's log; `log-1.txt` and `log-2.txt` are the two runs before |
| `screenshots/` | Screenshots, as `shot-YYYYMMDD-HHMMSS.png` |

`wii/tools/iso2ciso.py --trim GAME.iso game.ciso` shrinks a disc image to about 28 MB by leaving out its
padding.

## Controls

**In the launcher**

| | GameCube controller | Wii Remote | Classic Controller |
|---|---|---|---|
| Move | D-pad or stick | D-pad (Remote held sideways) or pointer | D-pad |
| Choose | A | 2 or A | A |
| Back | B | 1 or B | B |
| Shift (keyboard), look again (lobby list) | X | + | + |
| Exit | | HOME | HOME |

Hold Z on a GameCube controller while Nookery starts to skip the launcher and play.

**In the game**

- **GameCube controller:** as on a GameCube.
- **Classic Controller (or Pro):** plays as the GameCube controller of its Remote's player number when
  none is plugged into that port. A, B, X, Y, L, R and the D-pad are as labelled, + is START, the left
  stick is the control stick and the right one the C-stick, ZL is L pressed fully and ZR is Z.
- **Wii Remote 1 by itself:** its pointer is the PC port's mouse. A chooses, picks up and walks to where
  you point; B is the action button (hold it for the inventory); Up and Down on the D-pad scroll. The
  hand hides when a controller is used or the Remote is still for five seconds. A Remote alone has no
  START, X, Y, Z, L or R.
- **1** on any Wii Remote saves a screenshot (the Remote rumbles once).
- **HOME** held for a second, on a Remote or a Classic Controller, goes back to the Homebrew Channel.
- **RESET** on the console goes back to the Homebrew Channel; **POWER** turns the console off.

Leaving loses what you haven't saved.

## Settings

Change them in the launcher (**Settings**) or in `settings.ini` (`key = value` lines; `#` starts a
comment). On/off keys are `1`/`0`.

| Launcher row | Key | Default | |
|---|---|---|---|
| Fast text | `fast_text` | 0 | All text at the fast speed, as if B were held |
| Villager friendships | `villager_life` | 1 | Villagers make friends and rivals, and gossip about it ([what it does](../README.md#this-fork)) |
| Town life | `town_life` | 1 | Villagers get things to do around town ([what it does](../README.md#this-fork)) |
| Borderless acres | `borderless_acres` | 1 | The camera follows you across acre borders, with the villagers, buildings, trees and flowers of the 3x3 acres around you shown; 0 scrolls acre by acre, as the original does |
| No Resetti | `disable_resetti` | 0 | No visit from Resetti after leaving without saving. See [known issues](#known-issues) |
| Single-player shop upgrades | `disable_shop_visitor_req` | 0 | Nook's last shop upgrade without a visitor from another town |
| Villager follows traveler | `travel_move_out_chance` | 100 | How often the villager planning to move follows you to another town: 100, 50 or 10 (percent). See [known issues](#known-issues) |
| Widescreen | `widescreen` | 1 | 1 follows Wii Settings: on a 16:9 TV the view is wider (hor+) and menus stay 4:3. 0 is always 4:3 |
| Widescreen HUD at the edges | `hud_at_edges` | 0 | The corner HUD moves out to a 16:9 screen's edges |
| Wii Remote pointer in the game | `wii_remote_pointer` | 1 | Wii Remote 1's pointer as a mouse |
| NES games in 4:3 | `nes_aspect` | 1 | On a 16:9 TV, 1 keeps NES games 4:3 and 0 fills the screen |
| Volume | `master_volume` | 100 | 0 to 100. See [known issues](#known-issues) |
| (Online screens) | `online_name` | Villager | The name other players see, 15 characters at most |
| (Host your town) | `lobby_name`, `lobby_private`, `lobby_max_players` | | The last lobby you hosted |
| | `verbose` | 0 | The game's own debug lines in the log |

`settings.ini` also holds `stick_deadzone` and `cstick_deadzone` (the PC port's, unused here),
`lobby_server` (from before `lobby_servers.txt`, read only when that file is missing) and the
[test keys](#testing).

## Online play

Two to four players, console to console. The host plays their own town; guests travel to it by train
from theirs. Online play is on the Wii only: the PC port has none.

- **Host:** Online > Host your town. Name the lobby, make it public (listed) or private (joined by a
  six-character code), set a password if you like, and open it. Save your town at least once first:
  guests get the save as it was when the lobby opened.
- **Join:** Online > Browse lobbies, or Join with a code. The host's town is downloaded and takes slot
  B's place for the session; your own game then starts, and the train at your station goes to the
  host's town.

Players see each other outdoors in the host's town. What changes on the ground (items dropped, picked
up, planted, dug) is shared, the host's town having the last word, and guests keep the host's clock.
Villagers, shops, the weather and indoors run on each console by itself.

Everyone needs the same version of Nookery.

### Lobby servers

No server is listed out of the box, and without one only lobbies on your local network are found. To
play over the internet, someone runs the [lobby server](server/README.md) and each player adds its
address under Online > Lobby servers (or in `lobby_servers.txt`). The server lists lobbies and
introduces guests to hosts (UDP hole punching); the game itself, and the password, go between the
consoles only.

A hosting Wii listens on UDP port 28920. If guests can't reach it through your router, forward that
port to the Wii.

## Troubleshooting

`logs/log.txt` has the last run's log. A crash shows a stop screen and writes a report there.
A freeze (no new picture for 20 seconds) writes one too. Every report, and the log's first line,
names the build, and the two runs before are kept as `log-1.txt` and `log-2.txt`, so starting again
doesn't lose a report.
Once a minute outdoors, the log also says how long frames took.

`wii/tools/crash_report.py log.txt --elf nookery.elf` turns a report's addresses into functions and
source lines. It needs the symbols of the build that made the log: `build/wii/nookery.elf`, or the
`.elf.xz` that `make_release.py` writes beside a release zip.

### Known issues

- The launcher's **No Resetti**, **Villager follows traveler** and **Volume** rows are saved but don't
  change anything on the Wii yet.
- No GBA link (the island is reached without one, as in the PC port) and no streamed disc audio.
- Only the USA disc (`GAFE01`) is read.

## Building

You need devkitPro's `wii-dev` group and `ppc-zlib` (`dkp-pacman -S wii-dev ppc-zlib`; the preset
expects devkitPro in `/opt/devkitpro`), CMake 3.25 or newer, Ninja and Python 3.10 or newer.

```sh
git clone https://github.com/wize-logic/Nookery-Wii.git
cd Nookery-Wii
git submodule update --init extern/aurora
cmake --preset wii
cmake --build --preset wii            # build/wii/boot.dol, and nookery.elf with its symbols
python3 wii/tools/make_release.py     # build/release/nookery-wii-vX.Y.Z.zip
```

`make_release.py` also writes the build's symbols beside the zip (`nookery-wii-vX.Y.Z-BUILD.elf.xz`):
keep them with the release, for crash reports. Commit before building a release, so its build id is
a commit's.

The version is `wii/release/apps/nookery/meta.xml`'s. `-DNOOKERY_WII_NET=OFF` builds without online
play.

## Testing

Run `build/wii/boot.dol` in Dolphin with a folder synced in as the SD card. These files in that
folder's `apps/nookery` drive it without a player:

| File | |
|---|---|
| `autostart` | Empty file: skips the launcher |
| `input.txt` | Plays controller 1: `FRAME BUTTONS [STICK_X STICK_Y [CSTICK_X CSTICK_Y]]` lines (see `wii/src/pad.c`) |
| `launcher_input.txt`, `pointer.txt` | Play the launcher's controls and the Wii Remote's pointer |
| `nettest.txt` | Hosts or joins by itself: `host [private] [PASSWORD]`, or `join first`, `join server` or `join CODE`, with `[PASSWORD]` |
| `netrun.txt` | `net=listen:PORT`: the log is also served on that TCP port as the game runs (`nc WII_ADDRESS PORT`), on a console too |

Test keys in `settings.ini`:

| Key | |
|---|---|
| `trace_scenes` | Logs each scene change |
| `nes_test = N` | Starts NES game N from the town |
| `travel_test` | Takes the train to slot B's town |
| `net_echo`, `net_title_town`, `net_test_public`, `clock_test_skew` | Online play: yourself as a second player, the title screen as a town, the route between two networks, a clock set off by that many seconds |
| `life_test`, `life_test_topic` | 1: every chat is gossip (about topic N only); 2: villagers' distances and opinions in the log; 3 to 10: town life's tests (activities, animations and props, interruptions, gatherings, speech bubbles, tending the town, visitors, weather and night; `town_test` in `nk_netgame.c`) |
| `crash_test` | 1: a crash in town; 2: a freeze there (for the reports) |
| `screenshot_test`, `home_test = N` | A screenshot every 30 s; HOME held from N s on |
| `walk_test` | 1: the player walks a round of the town's acres by itself, with the actors, memory, disc reads and frame times logged; 2: also stops in each acre for a screenshot |
| `narrow_items_test` | Trees and flowers for the nearest four acres only, as without borderless acres (to compare) |

The lobby server has its own test: `python3 wii/server/tests/test_lobby.py`.

## Source layout

| Path | Contents |
|---|---|
| `wii/cmake/Wii.cmake`, `wii/nookery.ld` | The build (`TARGET_WII` and `ACGC_PORT`), with the game's tables in MEM2 |
| `wii/include` | Headers that stand in for the SDK's and the PC port's |
| `wii/lib/wiilib` | Helper libraries over libogc: disc images, files, the log |
| `wii/src` | The GameCube SDK over libogc: OS, VI, PAD, DVD, CARD, ARAM and AI, and the program's start |
| `wii/src/launcher` | The launcher |
| `wii/src/net` | Sockets, ENet, the lobby client and the session |
| `wii/src/netgame` | Online play in the game: the other players and the shared ground; the test keys |
| `wii/server` | The lobby server |
| `wii/tools` | Scripts for the build, releases, disc images and crash reports |
| `wii/release` | The SD card's files |

The game's own sources (`src/`, `include/`, `libs/dolphin`) are shared with the PC port. `ACGC_PORT`
marks code for any port, `TARGET_PC` and `TARGET_WII` code for one.

## Credits

[ac-decomp](https://github.com/ACreTeam/ac-decomp) and [Nookery](https://github.com/flyngmt/Nookery)'s
authors; [Aurora](https://github.com/encounter/aurora);
[ACGC-PC-Multiplayer](https://github.com/vsimochi/ACGC-PC-Multiplayer), the model for online play;
[devkitPro](https://devkitpro.org/) and libogc; [ENet](http://enet.bespin.org/) (MIT);
[fixNES](https://github.com/FIX94/fixNES) (MIT); [DejaVu fonts](https://dejavu-fonts.github.io/).
