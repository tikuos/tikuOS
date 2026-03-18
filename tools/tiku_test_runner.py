#!/usr/bin/env python3
"""Backward-compatible shim -- delegates to TikuBench/tikubench/runner.py."""
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "TikuBench"))
from tikubench.runner import main
if __name__ == "__main__":
    main()
