#!/usr/bin/env python3
"""Generate JSON benchmark data file."""
import json

data = {}
for n in (10, 50, 1000):
    data[f"obj_{n}"] = {f"key_{i}": i for i in range(n)}
    data[f"arr_{n}"] = list(range(n))

with open("test/data/bench.json", "w") as f:
    json.dump(data, f)

print(f"Wrote test/data/bench.json ({len(json.dumps(data))} bytes)")
