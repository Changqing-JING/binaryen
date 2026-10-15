#!/usr/bin/env python3
import os
import subprocess
import sys

if len(sys.argv) < 2:
    sys.exit(f"Usage: {sys.argv[0]} <module.wasm>")

wasm = os.path.abspath(sys.argv[1])
d8 = os.environ.get("D8", "/home/jcq/workspace/github/v8/v8/out/x64.release/d8")
interp = os.environ.get("WASM_INTERP", "wasm-interp")

runtimes = {
    "wasm-interp": [interp, "--run-all-exports", wasm],
}

for name, cmd in runtimes.items():
    print(f"\n=== Cache stats for {name} ===")
    subprocess.run(["valgrind", "--tool=cachegrind", "--cache-sim=yes", "--branch-sim=yes", *cmd])
