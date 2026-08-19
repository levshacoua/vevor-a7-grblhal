# VEVOR A7 10W → grblHAL: full reverse engineering & custom firmware

**Working custom firmware and the first complete hardware map for the VEVOR A7 10W laser engraver** — the machine built on the **AtomStack YZ-102 V1.0.9** controller (silkscreen **"LaserBox 40pro V1.0"**, sticker C10-0099-0AA1) with an **ESP32-S3-WROOM-1U** and a 74HC595 shift register driving the stepper drivers.

If your VEVOR A7 (or an AtomStack machine with this board) is bricked, stuck with the closed Chitu stock firmware, or you simply want LightBurn over Wi-Fi, jobs from the SD card and a web remote — this repository is for you.

*Reverse-engineered and documented by [Vlad (Lefty's Design)](https://levsha.co.ua) with Claude (Anthropic), August 2026. As far as we know this is the first published working custom firmware for this board — the stock firmware is closed, the pinout was undocumented, and FluidNC crashes on it (see below).*

> ⚠️ **Disclaimers.** This project is not affiliated with, endorsed by, or supported by VEVOR, AtomStack, Chitu Systems or Espressif. All product names are used only to identify compatible hardware. Flashing custom firmware **voids your warranty** and is done **entirely at your own risk**. A diode laser can blind and burn — never run it without protective eyewear, never leave it unattended, and always check local safety regulations. No stock/original firmware is distributed here — **back up your own flash before flashing anything** (instructions below).

---

## What you get

- **grblHAL 1.1f** on the stock board: real GRBL protocol over **USB, Telnet :23 (LightBurn "GRBL / Ethernet-TCP") and HTTP**
- **Jobs from the microSD card** with **auto-homing before every job**
- **Web remote** (`webui/run.html`, served from the board itself): list SD files, start, pause, resume, stop, live progress — works from any phone/browser, no cloud, no websocket
- **mDNS**: the machine is always reachable at `<hostname>.local`, plus optional **static IP** (`$321=0`, `$322`–`$324`) for LightBurn
- Full **hardware map** of the board (below) — measured with a multimeter, not guessed

## Hardware map (AtomStack YZ-102 V1.0.9 / "LaserBox 40pro V1.0")

MCU: **ESP32-S3-WROOM-1U (N8R2: 8 MB flash, 2 MB PSRAM)**, USB via CH340G (UART0, GPIO43/44). Two A4988-class stepper drivers (marking "4988ET") are fed by a single **74HC595** shift register — this is why FluidNC-style direct-GPIO configs can never move these motors.

| Function | Location |
|---|---|
| 74HC595 SER (data) | **GPIO45** |
| 74HC595 SRCLK (shift clock) | **GPIO46** |
| 74HC595 RCLK (latch) | **GPIO14** |
| 595 output bits | 1=Y_DIR, 2=Y_STEP, 3=Y_EN, 4=X_DIR, 5=X_STEP, 6=X_EN (EN active low) |
| **Hardware watchdog / charge pump** | **GPIO13** — needs a continuous ~8 kHz square wave or the drivers ignore STEP. X rail charges in ~10 s, **Y rail takes up to 2–3 minutes from cold power-on** |
| Laser TTL | **GPIO37** (LEDC PWM; the pad boots in hardware HOLD — firmware must `gpio_hold_dis()` + reset + drive LOW before PWM init, otherwise the laser glows at boot) |
| Limit switches | X = **GPIO18**, Y = **GPIO16** (active low, min ends) |
| microSD (SPI mode) | CS = **GPIO0**, CMD/MOSI = **GPIO35**, CLK = **GPIO36**, DAT0/MISO = **GPIO21**, card-detect = **GPIO48** (0 = inserted). DAT1/DAT2 not routed. Each line goes through a ferrite (L11/L12/L14/L13) next to the TF slot |
| Buzzer | GPIO6 |
| Probe header (unused) | GPIO15 (external pull-up) |
| JTAG header | TCK/TDO/TDI/TMS = GPIO39–42 |
| Stock GRBL params | 80 steps/mm, max 6000 mm/min, accel 1000, field 410×400 mm, $30=1000 |

**Known trap:** FluidNC v4.0.3 boot-loops (LoadProhibited) on ESP32-S3 with I2SO output — the shift-register motors cannot work there until that is fixed upstream. grblHAL's `i2s_out_s3` works.

**Upstream grblHAL bug fixed here:** on ESP32-S3 the spindle/laser LEDC *channel* config used `LEDC_SPEED_MODE_MAX` (an invalid value — it is the enum count, not a mode); the laser PWM never starts. The fix (`LEDC_LOW_SPEED_MODE`) is included in patch 0001 and was [merged upstream](https://github.com/grblHAL/ESP32/pull/214) on 2026-08-17.

## Flashing

**0. Back up your stock firmware first** (8 MB flash):

```
pip install esptool
esptool.py --chip esp32s3 --port /dev/cu.usbserial-XXXX --baud 230400 read_flash 0 0x800000 stock_backup_8mb.bin
```

Keep that file safe — it is your only way back (`write_flash 0 stock_backup_8mb.bin`). 460800 baud was unreliable on this board; 230400 is solid. No BOOT button needed — the board auto-enters bootloader via DTR/RTS.

**1. Flash this firmware:**

```
esptool.py --chip esp32s3 --port /dev/cu.usbserial-XXXX --baud 230400 erase_flash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-XXXX --baud 230400 write_flash 0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0x10000 firmware/firmware.bin
```

Later updates only need `write_flash 0x10000 firmware.bin` — settings survive.

**2. First-time setup** (over USB serial, 115200 baud, any terminal or LightBurn console):

```
$74=YourWifiSSID
$75=YourWifiPassword
$320=my-laser          ; hostname → http://my-laser.local
$70=37                 ; telnet + http + mDNS (websocket intentionally off)
$23=3 $22=3            ; homing (already defaults in this build)
```

Optional static IP: `$321=0`, `$322=192.168.x.y`, `$323=<gateway>`, `$324=255.255.255.0`, reboot.

**3. LightBurn:** add device → GRBL → **Ethernet/TCP** → the machine's IP. Set *Device Settings → End G-code* to `$H` if you want auto-homing after every job.

**4. Web remote:** upload `webui/run.html` to the board's internal flash:

```
curl -F "path=/" -F "file=@webui/run.html;filename=run.html" http://my-laser.local/files
```

Then open `http://my-laser.local/run.html` — SD file list, start (homing is automatic), pause/resume/stop, progress bar. Jobs also start over telnet with `$F=/file.nc`.

**Important:** after power-on wait ~3 minutes before the first move — the Y-axis driver's watchdog rail charges slowly (see hardware map). This is a property of the board, not the firmware.

## Building from source (GPLv3)

The firmware is [grblHAL](https://github.com/grblHAL/ESP32) (GPLv3) with this board's map and a few patches. Complete corresponding source = upstream at the pinned commits + `src/` of this repo:

| Base | Commit |
|---|---|
| grblHAL/ESP32 | `92e73fa9a71384347a1d520013f11bcd2a81a423` |
| submodule main/sdcard | `6ce881f28e5c9bada9b6501c1996f73a52bf102d` |
| submodule main/webui | `90bd97376fe6f59f4a22f636eb9f631dcdc40631` |

```
git clone --recurse-submodules https://github.com/grblHAL/ESP32
cd ESP32 && git checkout 92e73fa && git submodule update
cp <this repo>/src/my_machine_map.h main/boards/
cp <this repo>/src/my_plugin.c    main/
git apply <this repo>/src/patches/0001-esp32-driver-vevor-a7.patch
(cd main/sdcard && git apply .../0002-sdcard-auto-home-before-job.patch)
(cd main/webui  && git apply .../0003-webui-littlefs-fallback.patch)
platformio run -e vevor_a7
```

What the patches do: `0001` — watchdog pump on GPIO13 from boot, laser pad un-HOLD, the S3 LEDC fix, the `vevor_a7` PlatformIO env, STA static-IP support (`$321`); `0002` — auto-home before streaming a file; `0003` — serve static pages (the web remote) from littlefs. `my_plugin.c` adds the HTTP `RUNSD` command and an optional physical run-button on GPIO15.

## The story

The full reverse-engineering happened in ~3 days with a multimeter, MicroPython as a diagnostic REPL, and a lot of stubbornness: voltage-bisection to find the 595 pins, discovering the charge-pump watchdog on GPIO13 (the reason "nothing moves" on this board), the minutes-long Y-rail charge that looks exactly like a dead axis, the laser pad frozen by hardware HOLD, and a timing-signature scan that identified the real laser pin (GPIO37) without a logic analyzer. If there is interest I will publish the full chronicle — open an issue.

## Support this project

If this saved your machine (a bricked A7 is otherwise e-waste), you can say thanks — see the **Sponsor** button on this repo. Issues and PRs welcome.

## License

Code and patches: **GPLv3** (same as grblHAL) — see [LICENSE](LICENSE). Documentation and hardware map: **CC BY 4.0** — reuse freely with attribution to *Vlad, Lefty's Design (levsha.co.ua)*.
