# CHAOS: Compressed Hierarchical Addressable Object Structure

**CHAOS** is a next-generation binary serialization format built for **speed, compactness, and instant query access** on massive read-only datasets.
It bridges the gap between JSON’s flexibility and FlatBuffers’ performance — delivering **microsecond-level queries** without sacrificing schema freedom or self-description.

---

## Why CHAOS?

Unlike traditional binary formats that require predefined schemas or full data deserialization, CHAOS offers:

* **Self-Describing Format:** No schema files needed — CHAOS encodes metadata and dictionaries inline.
* **JSON-Compatible Hierarchy:** Mirrors JSON structure for easy integration with existing pipelines.
* **Instant Random Queries:** Decode only what you ask for — access deep fields in microseconds without loading full data.
* **Memory-Mapped Access:** Uses `mmap` for zero-copy reads on huge files (gigabyte-scale data with minimal RAM).
* **Compact Storage:** Typically 40–70% of original JSON size.
* **Parallel Engine:** Fully multithreaded encoding and decoding paths for maximum throughput.

---

## Architecture Overview

A `.chaos` file consists of:

1. **Header Size** – variable-encoded integer.
2. **Header Block** – includes:

   * Global dictionary (keys, type descriptors)
   * Entity count
   * Offset table width
   * Offset table (random access lookup table)
3. **Data Region** – encoded Objects, Lists, and Values in compact binary layout.
   References use table IDs; primitives are encoded directly (int, float, bool, string).

On decode, CHAOS memory-maps the file, reads only the header once, and lazily resolves offsets on demand.
That makes second-query lookups nearly instantaneous.

---

## Example Usage

### 1. Encode JSON → CHAOS

```bash
./chaos_tool encode parallel data.json data.chaos
```

### 2. Decode CHAOS → JSON

```bash
./chaos_tool decode parallel data.chaos
```

### 3. Query Specific Fields (Single)

```bash
./chaos_tool decode query data.chaos 45 location lat
```

### 4. Query Multiple Fields (Parallel Query Mode)

```bash
./chaos_tool decode query telemetry.chaos 45 location lat '|' 53 sensors temperature '|' 98 timestamp '|' 32 status
```

Output:

```
Query 1 (/45/location/lat): 36.692696 (695 ms)
Query 2 (/53/sensors/temperature): 5.14 (7 µs)
Query 3 (/98/timestamp): "2025-10-11T10:42:50.970520" (9 µs)
Query 4 (/32/status): "OK" (5 µs)
```

---

## Design Philosophy

CHAOS was created to power **AI pipelines, telemetry systems, and edge analytics** — where most operations require only selective reads from immutable data.
Instead of decoding terabytes just to fetch one field, CHAOS gives **direct indexed access** to the required nodes.

It’s ideal for:

* AI preprocessing and feature extraction pipelines
* Sensor/IoT telemetry stores
* Log archives and ML dataset caches
* Any system where **read-only structured data** must be queried in real time

---

## Build Instructions

### Prerequisites

* C++17 or newer (GCC ≥ 9, Clang ≥ 11)
* LZ4 development library (`liblz4-dev` on Linux, `brew install lz4` on macOS)

### Build

```bash
g++ -std=c++17 -O3 -I. \
    encoder.cpp encoder_parallel.cpp decoder.cpp decoder_parallel.cpp datastruct.cpp main.cpp \
    -llz4 -o chaos_tool_v1
```

**Current Version:** v1.0 — Core encoder/decoder/query engine (C++)

---

## Benchmark Snapshot (1 GB dataset)

| Operation            | Time                 |
| -------------------- | -------------------- |
| JSON encode          | 1241408 ms           |
| CHAOS encode         | 432451 ms (parallel) |
| JSON decode          | 426117 ms            |
| CHAOS decode         | 243311 ms            |
| JSON query           | 54561 ms             |
| CHAOS query (cold)   | 2388 ms              |
| CHAOS query (cached) | 5-9 µs               |

Developed by Gowri Sankar A, 2025. Compressed Hierarchical Addressable Object Structure with Selective Decoding
