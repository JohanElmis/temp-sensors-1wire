# 1-Wire temperature sensor hub (MOD-35 + ESP32)

An ESP32 reads DS18B20 temperature sensors through a Modułowo **MOD-35**
board. The MOD-35 carries a DS2482-100 (I2C → 1-Wire bridge) and 8 RJ45
jacks, all on one 1-Wire bus. Sensors plug in with ordinary network cables.
Readings go to **Home Assistant** through ESPHome's native API.

## Layout

```
esphome/
  mod35-1wire.yaml           # ESPHome config (ESP32-C3 SuperMini)
  mod35_ds2482_arduino.ino   # standalone Arduino test sketch
box/
  rj45_box.py                # CadQuery source for the wall-mount enclosure
  rj45_box_body.{stl,step}   # printable body
  rj45_box_lid.{stl,step}    # printable back plate / lid
  rj45_box_assembly.step     # body and lid together
docs/
  p557600-instrukcja-mod-35z.pdf   # MOD-35 manual (Polish)
```

## Wiring

MOD-35 `JP3` header to ESP32-C3 SuperMini:

| MOD-35 | ESP32-C3 |
| ------ | -------- |
| GND    | GND      |
| VCC    | 3V3      |
| SDA    | GPIO6    |
| SCL    | GPIO7    |

Use **3V3, not 5V**. The module's I2C pull-ups go to VCC and the ESP32 is
3.3 V only. The pull-ups are already on the board (10k on SDA/SCL, 4k7 on
the 1-Wire line), so no extra parts are needed.

RJ45 pinout: pin 2 = +VCC, pin 4 = 1-Wire, pins 1/3/5/8 = GND.

For another ESP32 board, change `board:` and the `sda`/`scl` substitutions in
the YAML. For a classic ESP32 DevKit, use pins 21 and 22.

## ESPHome

1. Create `esphome/secrets.yaml` (it is git-ignored) with these keys:
   `wifi_ssid`, `wifi_password`, `api_encryption_key`, `ota_password`.
2. Flash the config:
   ```
   esphome run esphome/mod35-1wire.yaml
   ```
3. On first boot, read the log for lines like
   `Found device 0x28xxxxxxxxxxxxxx`.
4. Paste each address into a `dallas_temp` entry under `sensor:`. Add one
   entry per DS18B20.

The I2C scan logs the DS2482 address. The jumpers set it to `0x18`–`0x1B`.
If it is not `0x18`, change `address:` under `ds248x:`.

Besides the temperatures, the node exposes WiFi signal, uptime and a restart
button.

## Arduino test sketch

`esphome/mod35_ds2482_arduino.ino` needs only `Wire.h`. It finds the DS2482,
lists every 1-Wire device and prints all temperatures to Serial every 10 s.
Use it to check the wiring and the sensors before moving to ESPHome. Set
`SDA_PIN` and `SCL_PIN` for your board.

## Enclosure

`box/rj45_box.py` builds a wall-mount box with a tilted front, so the jacks
face slightly downward. The back plate has four keyhole slots: put four small
countersunk screws in the wall and slide the box down onto them. Dimensions
are parameters at the top of the script.

```
pip install cadquery
python box/rj45_box.py <outdir>
```

The `.stl` files are ready to print.

## License

MIT. See [`LICENSE`](LICENSE).
