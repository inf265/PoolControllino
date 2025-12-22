# Pool Controllino HTTP API

## Configuration Endpoints

### Get Configuration

- **`GET /config`** - Returns an HTML page with a JSON editor interface to view and edit configuration

The page displays the current configuration in a JSON editor. Clicking "Apply" sends a POST request to update the configuration.

### Update Configuration

- **`POST /config`** - Updates the system configuration

**Request Format:**
- Content-Type: `application/json`
- Body: JSON object containing configuration parameters

**Response Format:**
```
HTTP/1.1 200 OK
Connection: close
```

**Example:**
```bash
curl -X POST http://pool-controllino.local/config \
  -H "Content-Type: application/json" \
  -d @config.json
```

### Configuration Parameters

All time values are in seconds unless otherwise specified. The configuration JSON accepts the following parameters:

#### System Settings
- **`updateTime`** (unsigned long): Main loop update interval in milliseconds (default: 3000)

#### Water Pump Schedule
- **`switchOn`** (string): Time to turn on water pump in HH:MM:SS format (default: "07:00:00")
- **`switchOff`** (string): Time to turn off water pump in HH:MM:SS format (default: "19:00:00")
- **`waterPumpRuntimeBeforeInjection`** (unsigned long): Seconds to wait after water pump starts before allowing injection (default: 120)
- **`waterPumpOffWhenFlowswitchOffTime`** (unsigned long): Seconds to wait after flow switch turns off before turning off water pump (default: 30)

#### pH Pump Settings
- **`phTargetValue`** (float): Target pH value (default: 7.4)
- **`phTargetValueHysterese`** (float): Hysteresis below target value to switch pump off (default: 0.1)
- **`phCalculationMValue`** (float): Linear calibration slope for pH sensor (default: 0.00447761194)
- **`phCalculationCValue`** (float): Linear calibration offset for pH sensor (default: -4.19403)
- **`phPumpCycleRunTime`** (unsigned long): Seconds the pH pump runs per cycle (default: 300)
- **`phPumpCyclePauseTime`** (unsigned long): Seconds the pH pump pauses between cycles (default: 300)
- **`phPumpMaxRuntime`** (unsigned long): Maximum total runtime per day in seconds (default: 1800 = 30 minutes)

#### Chlorine Pump Settings
- **`redoxTargetValue`** (uint16_t): Target redox/ORP value in mV (default: 465)
- **`redoxTargetValueHysterese`** (uint16_t): Hysteresis below target value to switch pump off (default: 50)
- **`redoxCalculationMValue`** (float): Linear calibration slope for redox sensor (default: 0.97765363128)
- **`redoxCalculationCValue`** (float): Linear calibration offset for redox sensor (default: -2154.888268)
- **`switchChlorOn`** (string): Time to start chlorine injection window in HH:MM:SS format (default: "10:00:00")
- **`switchChlorOff`** (string): Time to end chlorine injection window in HH:MM:SS format (default: "17:00:00")
- **`chlorinePumpCycleRunTime`** (unsigned long): Seconds the chlorine pump runs per cycle (default: 600)
- **`chlorinePumpCyclePauseTime`** (unsigned long): Seconds the chlorine pump pauses between cycles (default: 3600)
- **`chlorinePumpMaxRuntime`** (unsigned long): Maximum total runtime per day in seconds (default: 2700 = 45 minutes)

#### Manual Override Settings
- **`pumpManualOverrideTimeoutSeconds`** (unsigned long): Automatic timeout in seconds for manual pump control before returning to automatic mode (default: 1800 = 30 minutes)

### Configuration JSON Example

```json
{
  "updateTime": 3000,
  "switchOn": "07:00:00",
  "switchOff": "19:00:00",
  "phTargetValue": 7.4,
  "phTargetValueHysterese": 0.1,
  "phCalculationMValue": 0.00447761194,
  "phCalculationCValue": -4.19403,
  "phPumpCycleRunTime": 300,
  "phPumpCyclePauseTime": 300,
  "phPumpMaxRuntime": 1800,
  "chlorinePumpCycleRunTime": 600,
  "chlorinePumpCyclePauseTime": 3600,
  "chlorinePumpMaxRuntime": 2700,
  "redoxTargetValue": 465,
  "redoxTargetValueHysterese": 50,
  "redoxCalculationMValue": 0.97765363128,
  "redoxCalculationCValue": -2154.888268,
  "switchChlorOn": "10:00:00",
  "switchChlorOff": "17:00:00",
  "waterPumpRuntimeBeforeInjection": 120,
  "waterPumpOffWhenFlowswitchOffTime": 30,
  "pumpManualOverrideTimeoutSeconds": 1800
}
```

### Configuration Notes

- Configuration changes are persisted to EEPROM
- The controller will reboot after a successful configuration update
- Partial updates are supported - only provided parameters will be updated
- Time strings must be in HH:MM:SS format
- All time duration values are in seconds
- The configuration is validated before being saved

## Data/Status Endpoints

### Get All System Data

- **`GET /data`** - Returns all system data including sensor readings, pump states, and system status

**Response Format:**
- Content-Type: `text/html` (contains an HTML page with embedded JSON data)
- The JSON data is embedded in the HTML response and can be extracted from the JavaScript variable

**Response JSON Structure:**
```json
{
  "date": "08.10.2025 11:46:25",
  "temperature": "16.00",
  "housingtemperature": "21.13",
  "ph": "7.25",
  "phmedian": "7.5",
  "redox": "835.75",
  "redoxmedian": "835.75",
  "ph-pomp": "0",
  "redox-pomp": "0",
  "water-pomp": "1",
  "ph-man": "0",
  "redox-man": "0",
  "water-man": "0",
  "ph-man-rem": "0",
  "redox-man-rem": "0",
  "water-man-rem": "0",
  "waterflowswitch": "1",
  "powersupply": "1",
  "clientip": "",
  "error": "0",
  "errortext": "",
  "errortimestamp": "",
  "warning": "0",
  "warningtext": "",
  "warningtimestamp": "",
  "phadcvalue": "2556",
  "redoxadcvalue": "3059",
  "waterflowswitchadcvalue": "0",
  "powersupplyadcvalue": "0",
  "uptime": "12345"
}
```

**Field Descriptions:**
- `date`: Current date and time (DD.MM.YYYY HH:MM:SS format)
- `temperature`: Water temperature in Celsius
- `housingtemperature`: Housing temperature in Celsius
- `ph`: Current pH value
- `phmedian`: Median pH value (smoothed)
- `redox`: Current redox/ORP value in mV
- `redoxmedian`: Median redox value (smoothed)
- `ph-pomp`: pH pump state (0 = off, 1 = on)
- `redox-pomp`: Chlorine pump state (0 = off, 1 = on)
- `water-pomp`: Water pump state (0 = off, 1 = on)
- `ph-man`: pH pump manual override flag (0 = automatic control, 1 = manual override active)
- `redox-man`: Chlorine pump manual override flag (0 = automatic control, 1 = manual override active)
- `water-man`: Water pump manual override flag (0 = automatic control, 1 = manual override active)
- `ph-man-rem`: Remaining seconds until pH pump automatically returns to auto mode (0 if not in manual override)
- `redox-man-rem`: Remaining seconds until chlorine pump automatically returns to auto mode (0 if not in manual override)
- `water-man-rem`: Remaining seconds until water pump automatically returns to auto mode (0 if not in manual override)
- `waterflowswitch`: Flow switch state (0 = no flow, 1 = flow detected)
- `powersupply`: Power supply state (0 = off, 1 = on)
- `clientip`: Controller IP address
- `error`: Error flag (0 = no error, 1 = error present)
- `errortext`: Error message (if error present)
- `errortimestamp`: Timestamp of error (if error present)
- `warning`: Warning flag (0 = no warning, 1 = warning present)
- `warningtext`: Warning message (if warning present)
- `warningtimestamp`: Timestamp of warning (if warning present)
- `phadcvalue`: Raw ADC value for pH sensor
- `redoxadcvalue`: Raw ADC value for redox sensor
- `waterflowswitchadcvalue`: Raw ADC value for flow switch
- `powersupplyadcvalue`: Raw ADC value for power supply
- `uptime`: System uptime in seconds

**Example:**
```bash
curl http://pool-controllino.local/data
```

**Note:** This endpoint returns an HTML page with embedded JSON. To extract just the JSON data programmatically, you can parse the HTML response and extract the JSON from the JavaScript variable, or use a JSON parser that can handle the HTML response.

## Pump Control Endpoints

Manual control of pumps via HTTP. All endpoints accept both GET and POST requests.

### Control Endpoints

Control individual pumps manually (bypasses automatic control):

- **Water Pump:**
  - `GET/POST /pump/water/on` - Turn water pump ON manually
  - `GET/POST /pump/water/off` - Turn water pump OFF manually
  - `GET/POST /pump/water/auto` - Return water pump to automatic control

- **pH Pump:**
  - `GET/POST /pump/ph/on` - Turn pH pump ON manually
  - `GET/POST /pump/ph/off` - Turn pH pump OFF manually
  - `GET/POST /pump/ph/auto` - Return pH pump to automatic control

- **Chlorine Pump:**
  - `GET/POST /pump/chlorine/on` - Turn chlorine pump ON manually
  - `GET/POST /pump/chlorine/off` - Turn chlorine pump OFF manually
  - `GET/POST /pump/chlorine/auto` - Return chlorine pump to automatic control

### Response Format

All control endpoints return JSON:
```json
{
  "success": true,
  "message": "Water pump manually turned ON"
}
```

### Examples

**Turn water pump on:**
```bash
curl -X POST http://pool-controllino.local/pump/water/on
```

**Turn pH pump off:**
```bash
curl -X POST http://pool-controllino.local/pump/ph/off
```

**Return chlorine pump to automatic:**
```bash
curl -X POST http://pool-controllino.local/pump/chlorine/auto
```

**Get all system data (including pump states):**
```bash
curl http://pool-controllino.local/data
```

## Manual Override

### Overview

Manual override allows you to temporarily take control of individual pumps, bypassing the automatic control logic. This feature is useful for:
- Testing and maintenance
- Emergency situations requiring immediate pump control
- Temporary adjustments that differ from the automatic schedule
- Troubleshooting pump behavior

When manual override is activated, the specified pump operates in manual mode until:
1. The automatic timeout expires (default: 30 minutes), OR
2. You explicitly return it to automatic mode via the `/auto` endpoint

### How It Works

#### Activation

Manual override is activated by sending a request to turn a pump `on` or `off`:
- `GET/POST /pump/{type}/on` - Activates manual override and turns the pump ON
- `GET/POST /pump/{type}/off` - Activates manual override and turns the pump OFF
- `GET/POST /pump/{type}/auto` - Deactivates manual override and returns to automatic control

When activated, the controller:
1. Sets the manual override flag for the specified pump (`manualOverride = true`)
2. Records the current timestamp as the override start time (resets the timeout timer)
3. Immediately applies the requested pump state (on/off)
4. Begins counting down the timeout period

⚠️ **Important**: Once you enter manual mode, you **remain in manual mode** until you either:
- Explicitly call `/pump/{type}/auto` to exit manual mode, OR
- Wait for the timeout to expire

Switching the pump state while already in manual mode (e.g., calling `/on` then `/off`, or vice versa) **keeps you in manual mode** - it only changes the desired pump state. Each state change also **resets the timeout timer** to start counting from zero again.

#### State Management

While in manual override mode:
- The pump state is controlled directly by the manual override command
- Automatic control logic is **completely bypassed** - sensors, schedules, and safety limits are ignored
- The controller tracks the elapsed time since override activation
- Pump state changes take effect on the next controller loop iteration (typically within 3 seconds)

#### Timeout Mechanism

Each manual override has an automatic timeout to prevent accidental extended manual operation:

- **Default timeout**: 1800 seconds (30 minutes)
- **Configurable**: Set via `pumpManualOverrideTimeoutSeconds` in the configuration
- **Automatic return**: When the timeout expires, the pump automatically returns to automatic control mode
- **State on return**: The pump returns to whatever state the automatic control logic determines (based on sensors, schedules, etc.)

The timeout countdown starts when manual override is activated. You can check the remaining time via the `/data` endpoint:
- `ph-man-rem`: Remaining seconds for pH pump manual override
- `redox-man-rem`: Remaining seconds for chlorine pump manual override  
- `water-man-rem`: Remaining seconds for water pump manual override

These fields show `0` when the pump is in automatic mode (no manual override active).

#### Checking Override Status

Query the `/data` endpoint to check manual override status:

```bash
curl http://pool-controllino.local/data
```

The response includes these fields for each pump:

| Field | Description |
|-------|-------------|
| `ph-man` | pH pump manual override flag (0 = auto, 1 = manual) |
| `redox-man` | Chlorine pump manual override flag (0 = auto, 1 = manual) |
| `water-man` | Water pump manual override flag (0 = auto, 1 = manual) |
| `ph-man-rem` | Remaining seconds until pH pump returns to auto (0 if not in manual) |
| `redox-man-rem` | Remaining seconds until chlorine pump returns to auto (0 if not in manual) |
| `water-man-rem` | Remaining seconds until water pump returns to auto (0 if not in manual) |

### Safety Considerations

#### Water Pump

- **Full manual control**: When in manual override, the water pump operates independently of:
  - Time-based schedule (`switchOn`/`switchOff`)
  - Flow switch state
  - All automatic safety checks

⚠️ **Warning**: Manual water pump override bypasses safety interlocks. Use with caution.

#### pH and Chlorine Pumps

- **Manual state control**: When in manual override, injection pumps are controlled directly
- **Runtime limits bypassed**: Daily runtime limits (`phPumpMaxRuntime`, `chlorinePumpMaxRuntime`) are not enforced in manual mode
- **Cycle times bypassed**: Cycle run/pause times are ignored in manual mode
- **Sensor values ignored**: pH and redox sensor readings do not affect pump state in manual mode
- **Schedule ignored**: Chlorine injection time window is bypassed

⚠️ **Important**: The injection pumps' manual override calls `setManualState()`, which directly controls the pump hardware. However, the controller code still includes safety checks that may turn off injection pumps if the water pump is off, even in manual mode. This is a design consideration for safety.

### Usage Examples

#### Example 1: Temporary pH Adjustment

You need to quickly raise the pH level:

```bash
# Activate manual override and turn on pH pump
curl -X POST http://pool-controllino.local/pump/ph/on

# Monitor remaining time (check every few minutes)
curl http://pool-controllino.local/data | grep "ph-man-rem"

# When done, manually return to auto (or wait for timeout)
curl -X POST http://pool-controllino.local/pump/ph/auto
```

#### Example 2: Emergency Water Pump Shutdown

You need to immediately turn off the water pump:

```bash
# Turn off water pump manually
curl -X POST http://pool-controllino.local/pump/water/off

# Check status to confirm
curl http://pool-controllino.local/data | grep "water"
```

The pump will automatically return to automatic control after the timeout expires, or you can restore it manually.

#### Example 3: Extended Manual Operation

For maintenance requiring more than the default 1-hour timeout:

1. Increase the timeout via configuration:
```bash
curl -X POST http://pool-controllino.local/config \
  -H "Content-Type: application/json" \
  -d '{"pumpManualOverrideTimeoutSeconds": 7200}'
```
(Note: Configuration changes require a reboot)

2. Activate manual override as needed

3. Monitor remaining time via `/data` endpoint

### Best Practices

1. **Monitor remaining time**: Regularly check `*-man-rem` fields to know when manual override will expire
2. **Use short timeouts for testing**: Keep timeouts short (e.g., 300 seconds) when testing to reduce risk
3. **Explicitly disable when done**: Use `/auto` endpoint to return to automatic mode when manual control is no longer needed, rather than waiting for timeout
4. **Verify state after override**: After manual override ends (timeout or explicit disable), verify the pump returns to expected automatic behavior
5. **Document manual interventions**: Keep a log of manual override activations for troubleshooting and maintenance records
6. **Safety first**: Remember that manual override bypasses safety limits - use only when necessary and understand the implications

### Technical Details

#### Implementation Notes

- Manual override state is stored in `PoolControlContext::data`:
  - `*PumpManualOverride` (bool): Flag indicating if manual override is active
  - `*PumpManualState` (bool): The desired pump state (true = on, false = off)
  - `*PumpManualOverrideSince` (DateTime): Timestamp when override was activated

- Timeout checking occurs in the controller's main loop:
  - Each pump controller (`PhController`, `ChlorineController`, `WaterpumpController`) checks for timeout expiration during `run()`
  - When timeout expires, the manual override flag is automatically cleared
  - The pump then falls back to automatic control logic

- Manual override takes effect on the **next controller loop iteration** after the HTTP request is processed (typically within 3 seconds, depending on `updateTime` configuration)

### Configuration

Manual override timeout is configurable via the `/config` endpoint:

```json
{
  "pumpManualOverrideTimeoutSeconds": 1800
}
```

- **Type**: `unsigned long` (seconds)
- **Default**: 1800 (30 minutes)
- **Range**: 1 to 86400 (1 second to 24 hours)
- **Persistent**: Saved to EEPROM and survives reboots

