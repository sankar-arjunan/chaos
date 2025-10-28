# CHAOS: Compressed Hierarchical Addressable Object Structure

**CHAOS** is a next-generation binary serialization format optimized for **speed**, **compactness**, and **microsecond-level query access**.
It bridges JSON’s flexibility with FlatBuffers-level performance — enabling **direct field access without full decoding**.

---

## Features

* **Self-Describing:** No schemas needed. Every `.chaos` file encodes its own structure and dictionary.
* **JSON-Compatible:** Fully hierarchical, supports arbitrary nesting of lists and objects.
* **Selective Decoding:** Reads only the queried fields.
* **Memory-Mapped:** Zero-copy read performance on gigabyte-scale data.
* **Compact Encoding:** 40–70% smaller than JSON.
* **Parallel Engine:** Multithreaded encoding and decoding.
* **Python API:** High-performance bindings (`pychaos`) for AI and analytics pipelines.

---

## File Layout

1. **Header Size** (variable integer)
2. **Header Block**

   * Global key dictionary
   * Object count
   * Offset table
3. **Data Region** (compact binary encoding of primitives, lists, and objects)

Memory mapping ensures that subsequent queries reuse the already-loaded header and offsets for near-zero latency lookups.

---

## Command-Line Examples

### Encode JSON → CHAOS

```bash
./chaos_tool encode parallel data.json data.chaos
```

### Decode CHAOS → JSON

```bash
./chaos_tool decode parallel data.chaos
```

### Selective Query

```bash
./chaos_tool decode query data.chaos 42 telemetry temperature
```

### Multi-Query

```bash
./chaos_tool decode query data.chaos 42 telemetry temp '|' 45 timestamp
```

Output:

```
Query 1 (/42/telemetry/temp): 22.4 (1.3 ms)
Query 2 (/45/timestamp): "2025-10-11T10:41:23.970520" (9 µs)
```

---

## Python Integration (`pychaos`)

### Build

```bash
make pychaos
```

This builds `pychaos.so`, the Python extension module.

### Encode and Query

```python
import pychaos

# Encode JSON to CHAOS
ms = pychaos.encode("JSON/sample.json", "CHAOS/sample.chaos")
print("Encoded in", ms, "ms")

# Query fields
queries = [
    ["0", "device"],
    ["1", "timestamp"],
    ["1", "sensor", "temperature"]
]

results, t, dec = pychaos.query("CHAOS/sample.chaos", queries)
print("Query:", results, "Time:", t, "ms")

# Reuse decoder for faster queries
queries2 = [["2", "sensor", "pressure"]]
results2, t2, _ = pychaos.query("CHAOS/sample.chaos", queries2, dec)
print("Second query:", results2, "Time:", t2, "ms")
```

---

## Performance Snapshot (1 GB dataset)

| Operation             | Time (ms) | Speedup vs JSON |
| --------------------- | --------- | --------------- |
| JSON full parse       | 237987    | 1×              |
| CHAOS full decode     | 72965     | 3.3×            |
| CHAOS selective query | 953       | 249.7×          |

---

## Build System

### Build all targets

```bash
make
```

### Clean

```bash
make clean
```

**Version:** v1.1 – includes full Python bindings for encoding, decoding, and selective query sessions.

Developed by **Gowri Sankar A**, 2025.
