import serial
import json

# ── Constants ──────────────────────────────────────────────
ADC_MAX_RAW        = 4095
ADC_VREF           = 4.095
TRANSDUCER_OUT_MAX = 5.0
TRANSDUCER_IN_MAX  = 500.0

SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE   = 115200

# ── Conversion ─────────────────────────────────────────────
def raw_to_ac_voltage(raw: int) -> float:
    adc_v = (raw / ADC_MAX_RAW) * ADC_VREF
    return (adc_v / TRANSDUCER_OUT_MAX) * TRANSDUCER_IN_MAX

# ── Main ───────────────────────────────────────────────────
def main():
    print(f"Opening {SERIAL_PORT} at {BAUD_RATE} baud...")
    with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
        print("Listening for data...\n")
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            # Try to extract JSON from the line
            # Coordinator may print extra log text, so find the { } block
            start = line.find("{")
            end   = line.rfind("}") + 1
            if start == -1 or end == 0:
                continue  # no JSON found on this line

            json_str = line[start:end]

            try:
                data = json.loads(json_str)
            except json.JSONDecodeError:
                print(f"  [WARN] Could not parse: {json_str}")
                continue

            print("─" * 45)
            for key, raw_val in data.items():
                ac = raw_to_ac_voltage(int(raw_val))
                print(f"  {key}: {ac:.2f} VAC  (raw={int(raw_val)})")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
    except serial.SerialException as e:
        print(f"Serial error: {e}")