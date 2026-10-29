"""Validate the raw capture before interpreting a firmware reset boundary."""
import hashlib


def validate_boot_capture(raw_bytes, records, firmware_id):
    """Return strict ASCII rows from one BOOT and evidence of the reset prefix.

    JSONL is the capture reader's lossy ASCII projection. Compare it to the
    binary source directly; never attempt to reconstruct non-ASCII raw bytes
    from replacement characters. Only bytes preceding the selected BOOT may
    contain non-ASCII data.
    """
    if not isinstance(raw_bytes, bytes) or not raw_bytes.endswith(b'\n'):
        raise ValueError('raw capture has an incomplete tail')
    parts = raw_bytes[:-1].split(b'\n')
    if len(parts) != len(records):
        raise ValueError('raw/JSON row count mismatch')
    for part, record in zip(parts, records):
        if (not isinstance(record, dict) or 'incomplete_tail_hex' in record
                or record.get('line') != part.rstrip(b'\r').decode('ascii', errors='replace')):
            raise ValueError('raw/JSON projection mismatch')
    marker = b'$BOOT,FW=' + firmware_id.encode('ascii') + b','
    hits = [(i, offset) for i, part in enumerate(parts)
            for offset in range(len(part)) if part.startswith(marker, offset)]
    if len(hits) != 1:
        raise ValueError('expected exactly one firmware BOOT')
    boot_at, offset = hits[0]
    if offset >= 256:
        raise ValueError('oversized pre-boot fragment')
    prefix = parts[boot_at][:offset]
    selected = [parts[boot_at][offset:], *parts[boot_at + 1:]]
    try:
        lines = [part.rstrip(b'\r').decode('ascii') for part in selected]
    except UnicodeDecodeError as error:
        raise ValueError('non-ASCII byte after firmware BOOT') from error
    if sum(line.count('$BOOT,') for line in lines) != 1:
        raise ValueError('additional BOOT after firmware BOOT')
    pre_boot = b'\n'.join(parts[:boot_at]) + prefix
    proof = {
        'discarded_pre_boot_lines': boot_at,
        'discarded_prefix_hex': prefix.hex(),
        'pre_boot_nonascii_bytes': sum(byte >= 128 for byte in pre_boot),
        'raw_json_projection_matches': True,
        'raw_sha256': hashlib.sha256(raw_bytes).hexdigest(),
        'normalized_sha256': hashlib.sha256(('\n'.join(lines) + '\n').encode('ascii')).hexdigest(),
        'raw_rows': len(parts),
        'normalized_rows': len(lines),
    }
    return lines, proof
