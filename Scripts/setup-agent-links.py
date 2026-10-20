#!/usr/bin/env python3
"""Link Claude Code's skills directory to the shared .agents/skills directory.

Windows uses a local NTFS junction (no symlink privilege needed);
POSIX platforms use a relative symbolic link. Do not commit the generated link.
"""

import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    source = root / ".agents" / "skills"
    target = root / ".claude" / "skills"

    if not source.is_dir():
        print(f"Missing shared skills directory: {source}", file=sys.stderr)
        return 1

    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_symlink() or target.exists():
        if target.is_dir() and source.samefile(target):
            print(f"Already linked: {target}")
            return 0
        print(f"Refusing to replace existing path: {target}", file=sys.stderr)
        return 1

    if os.name == "nt":
        command = ["cmd", "/c", "mklink", "/J", str(target), str(source)]
        result = subprocess.run(command, check=False)
        if result.returncode:
            return result.returncode
    else:
        target.symlink_to(Path("..") / ".agents" / "skills", target_is_directory=True)

    if not target.is_dir() or not source.samefile(target):
        print(f"Link verification failed: {target}", file=sys.stderr)
        return 1

    print(f"Verified: {target} -> {source}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
