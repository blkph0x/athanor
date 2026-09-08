#!/usr/bin/env python3
"""
Build sorted SHA-256 password-deny set for Athanor (DEC-0046).

Default: curated common passwords (not full rockyou in git).
Optional:  python tools/gen_pwd_deny.py --rockyou /path/to/rockyou.txt

Output: android/assets/atn_pwd_deny.bin
  uint32_be count | count * 32-byte SHA-256 digests (sorted, unique)
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "android" / "assets" / "atn_pwd_deny.bin"

# Curated commons — enough to reject obvious choices; builders add rockyou locally.
CURATED = [
    "password", "password1", "Password1", "123456", "12345678", "123456789",
    "1234567890", "qwerty", "qwerty123", "abc123", "letmein", "welcome",
    "admin", "admin123", "root", "passw0rd", "iloveyou", "monkey", "dragon",
    "master", "login", "princess", "football", "baseball", "starwars",
    "whatever", "trustno1", "sunshine", "ashley", "bailey", "shadow",
    "superman", "michael", "jennifer", "hunter", "buster", "soccer",
    "harley", "batman", "andrew", "tigger", "charlie", "robert", "thomas",
    "hockey", "ranger", "daniel", "hannah", "maggie", "jessica", "george",
    "computer", "michelle", "freedom", "cookie", "hello", "charlie1",
    "Password123", "Passw0rd!", "P@ssw0rd", "changeme", "default",
    "samsung", "android", "phone", "secret", "test", "test123",
]


def digest(pw: str) -> bytes:
    return hashlib.sha256(pw.encode("utf-8", errors="ignore")).digest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rockyou", type=Path, help="optional rockyou.txt (local)")
    ap.add_argument("--extra", type=Path, help="extra one-password-per-line file")
    ap.add_argument("--max", type=int, default=2_000_000,
                    help="cap hashes when ingesting large dumps")
    args = ap.parse_args()

    hashes = {digest(p) for p in CURATED}
    for path in (args.rockyou, args.extra):
        if path is None:
            continue
        if not path.is_file():
            raise SystemExit(f"missing {path}")
        n = 0
        with path.open("rb") as f:
            for raw in f:
                if n >= args.max:
                    break
                line = raw.strip()
                if not line or line.startswith(b"#"):
                    continue
                try:
                    s = line.decode("utf-8", errors="ignore")
                except Exception:
                    continue
                hashes.add(digest(s))
                n += 1
        print(f"ingested {n} lines from {path}")

    ordered = sorted(hashes)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack(">I", len(ordered)))
        for h in ordered:
            f.write(h)
    print(f"wrote {OUT} count={len(ordered)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
