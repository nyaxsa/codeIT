# Multithreaded C++ File Compression Tool (RLE)

High-performance, **chunked multithreaded** file compressor/decompressor using **Run-Length Encoding (RLE)**.

It splits the input into fixed-size chunks and processes them in parallel using `std::thread`, uses `std::mutex` for safe
console output / error propagation, and uses `std::chrono` for **execution time + throughput** measurement.

## Features

- **File compression** (`compress`)
- **File decompression** (`decompress`)
- **Multithreaded chunk processing** (work-stealing via atomic index)
- **Execution time comparison**: single-thread vs multi-thread (`--compare 1`, default)
- **Error handling**: validates container header and RLE stream integrity
- **Clear console output**: timing, MiB/s, size ratio, and speedup

## Build (macOS / Linux)

```bash
g++ -std=c++17 -O3 -pthread -Wall -Wextra -pedantic main.cpp -o mt_rle
```

## Usage

### Compress

```bash
./mt_rle compress -i input.bin -o output.rlec --threads 8 --chunk 4194304 --compare 1
```

### Decompress

```bash
./mt_rle decompress -i output.rlec -o restored.bin --threads 8 --compare 1
```

## Output files in compare mode

When `--compare 1`, the program writes **two** outputs so you can keep both results:

- Single-thread output: the `-o` path you provided
- Multithread output: the same path with `.mt` appended

## Notes

- RLE is intentionally simple; it shines on repetitive data but may **expand** random/incompressible data.
- The produced `.rlec` format is a tiny custom container with a header and per-chunk lengths for robust decoding.

# codetechit-sol
# codetechit-sol
# codetechit-sol
# codeIT
# codeIT
# codeIT
