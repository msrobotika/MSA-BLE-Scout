"""Explicit full-flash recovery. Requires the user's backup, hash and target MAC."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("backup", type=Path)
    p.add_argument("--sha256", required=True)
    p.add_argument("--mac", required=True)
    p.add_argument("--port", default="COM4")
    args = p.parse_args()
    if args.backup.stat().st_size != 4194304 or hashlib.sha256(args.backup.read_bytes()).hexdigest() != args.sha256.lower():
        raise SystemExit("Backup size or SHA-256 mismatch; flash has not been written")
    base = [sys.executable, "-m", "esptool", "--port", args.port, "--baud", "115200"]
    identified = subprocess.run(base + ["--no-stub", "--after", "no-reset", "chip-id"], capture_output=True, text=True, timeout=30)
    if identified.returncode or args.mac.lower() not in identified.stdout.lower():
        raise SystemExit("Target MAC could not be confirmed; flash has not been written")
    subprocess.run(base + ["--before", "no-reset", "--after", "hard-reset", "write-flash", "0", str(args.backup)], check=True)

if __name__ == "__main__":
    main()
