#!/usr/bin/env python3
"""Lightweight shim -- delegates to TikuBench/tikubench/ntp_server.py."""
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "TikuBench"))
from tikubench.ntp_server import main
if __name__ == "__main__":
    main()
