#!/usr/bin/env python3
"""Read-only server observations. Use --help for explicit inputs and limits."""
import sys
from pathlib import Path


if __name__ == "__main__":
    # The reviewed tool directory is trusted; private input directories are not.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from identity_check.cli import main
    raise SystemExit(main())
