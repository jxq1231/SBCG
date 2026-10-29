"""Check M3.1 serial evidence; counts alone do not verify acoustic response."""
def check_audio_rows(initialization, rows, minimum_rows=30):
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    expected = dict(RESULT="OK", NOMINAL_HZ="48000", DERIVED_MILLI_HZ="47991071",
                    START_ERR="0", CODEC="0", I2C_ERR="00000000", REG="FFFF", WRITES="24")
    require(all(initialization.get(k) == v for k, v in expected.items()), "audio initialization failed")
    require(minimum_rows >= 2 and len(rows) >= minimum_rows, "audio window too short")
    previous = None
    try:
        for row in rows:
            sequence, half, full, good, dropped, errors, frames, rate = (
                int(row[k]) for k in ("SEQ", "H", "F", "OK", "DROP", "ERR", "FRAMES", "RATE_HZ"))
            require(row["STATE"] == "RUN" and row["VALID"] == "1", "audio invalid/stalled")
            require(dropped == errors == int(row["HAL"], 16) == int(row["DMA"], 16) == 0,
                    "audio capture errors or lost blocks")
            require(0 <= half - full <= 1 and frames == (half + full) * 8192 and
                    0 <= half + full - good <= 1, "audio ownership/frame accounting mismatch")
            require(47511 <= rate <= 48471, "audio rate outside derived +/-1% sanity range")
            for channel in range(2):
                peak, rms = int(row[f"P{channel}"]), int(row[f"R{channel}"])
                require(0 <= rms <= peak <= 32768, "invalid PCM amplitude")
            if previous:
                require(sequence == previous[0]+1 and half >= previous[1] and
                        full >= previous[2] and frames > previous[3] and good > previous[4],
                        "audio records missing, reordered or frozen")
            previous = sequence, half, full, frames, good
        for channel in range(2):
            peaks = [int(row[f"P{channel}"]) for row in rows]
            rms = [int(row[f"R{channel}"]) for row in rows]
            require(len(set(peaks)) > 1 and max(rms) > 0 and min(peaks) < 32768,
                    f"channel {channel} zero, constant or continuously clipped")
    except (KeyError, TypeError) as error:
        raise ValueError(f"missing/invalid audio field: {error}") from error
    return {"valid_rows": len(rows), "last_frames": int(rows[-1]["FRAMES"]),
            "last_half": int(rows[-1]["H"]), "last_full": int(rows[-1]["F"]),
            "rate_hz_min": min(int(row["RATE_HZ"]) for row in rows),
            "rate_hz_max": max(int(row["RATE_HZ"]) for row in rows),
            "scope": "single-board capture continuity; controlled acoustic response still needs phase comparison"}
