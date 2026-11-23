#!/usr/bin/env python3
"""Entry point for the restored phase-one identity/configuration/UI checks.

Uses the same isolated, disposable fixtures as check-agent-management.py.
No restart, hardware, OAuth or real model requests.
"""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name('check-agent-management.py')), run_name='__main__')
