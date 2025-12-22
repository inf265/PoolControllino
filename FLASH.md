# Flashing PoolControllino to the Controller

## Prerequisites

1. **Hardware Setup:**
   - Connect the Controllino MEGA to your computer via USB
   - Power on the Controllino (can use USB or external power supply)

2. **Software Setup:**
   - Ensure you have `avrdude` installed:
     ```bash
     sudo apt-get install avrdude  # Debian/Ubuntu
     ```
   - Check that your user is in the `dialout` group (needed for serial port access):
     ```bash
     groups  # Check if 'dialout' is listed
     sudo usermod -a -G dialout $USER  # Add if missing (requires logout/login)
     ```

## Finding the Correct Serial Port

1. **Before connecting:** Note which USB devices are available:
   ```bash
   ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
   ```

2. **After connecting:** Run again to see which new device appeared:
   ```bash
   ls -la /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
   ```

3. **Common ports:**
   - `/dev/ttyACM0` - Most common for Controllino via USB
   - `/dev/ttyUSB0` - Sometimes used on Linux with USB-to-serial adapters

## Upload Methods

### Method 1: Using CMake Upload Target (Recommended)

1. **Build the firmware** (if not already built):
   ```bash
   cd build
   cmake --build . -j -t PoolControllino
   ```

2. **Put Controllino in bootloader mode:**
   - Quickly press the RESET button **twice** within ~1 second
   - The LED should start blinking rapidly (indicating bootloader mode)
   - You have ~8 seconds before it exits bootloader mode

3. **Upload immediately:**
   ```bash
   make upload-PoolControllino
   ```

   Or if you need to specify a different port:
   ```bash
   cmake .. -DARDUINO_PORT=/dev/ttyACM0
   make upload-PoolControllino
   ```

### Method 2: Manual Upload with avrdude

If the CMake method doesn't work, you can use avrdude directly:

1. **Put Controllino in bootloader mode** (press RESET twice quickly)

2. **Upload the .hex file:**
   ```bash
   avrdude -C /etc/avrdude.conf \
           -p atmega2560 \
           -c wiring \
           -P /dev/ttyACM0 \
           -b 115200 \
           -D \
           -U flash:w:PoolControllino.hex:i
   ```

   Replace `/dev/ttyACM0` with your actual port if different.

### Method 3: Using Arduino IDE Upload Button

The Arduino IDE can also be used:
1. Open `PoolControllino.cpp` in Arduino IDE
2. Select board: **Tools → Board → Controllino MEGA**
3. Select port: **Tools → Port → /dev/ttyACM0** (or your port)
4. Click **Upload** button
5. When prompted, press RESET button twice quickly

## Troubleshooting

### "Permission denied" error
```bash
sudo usermod -a -G dialout $USER
# Then logout and login again
```

### "timeout communicating with programmer"
- Make sure you pressed RESET **twice quickly** to enter bootloader mode
- Try a different USB port
- Check USB cable (use a data cable, not just a charging cable)
- Try unplugging and reconnecting USB
- On some systems, you may need to:
  ```bash
  sudo chmod 666 /dev/ttyACM0
  ```

### "device busy" error
- Close any serial monitors or other programs using the port
- Unplug and reconnect the USB cable

### Wrong device detected
- Verify you're using the correct port
- Check `dmesg | tail` after connecting to see what device was detected

### Upload keeps failing
1. **Timing issue:** The bootloader mode only lasts ~8 seconds. Try this automated approach:
   ```bash
   # In one terminal, monitor for bootloader:
   stty -F /dev/ttyACM0 1200 cs8 -cstopb -parenb
   
   # In another terminal, immediately after:
   make upload-PoolControllino
   ```

2. **Alternative:** Use an ISP programmer for direct flashing (bypasses bootloader)

## Verification

After a successful upload, you should see:
```
avrdude: AVR device initialized and ready to accept instructions
Reading | ################################################## | 100% 0.00s
avrdude: Device signature = 0x1e9801 (probably m2560)
avrdude: reading input file "PoolControllino.hex"
avrdude: writing flash (xxxxx bytes):
Writing | ################################################## | 100% x.xxs
avrdude: xxxxx bytes of flash written
avrdude: verifying flash memory against PoolControllino.hex:
avrdude: load data flash data from input file PoolControllino.hex:
avrdude: input file PoolControllino.hex contains xxxxx bytes
avrdude: reading on-chip flash data:
Reading | ################################################## | 100% x.xxs
avrdude: verifying ...
avrdude: xxxxx bytes of flash verified
avrdude done.  Thank you.
```

The controller will automatically reboot and start running the new firmware.


