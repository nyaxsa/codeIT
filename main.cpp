#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Byte = std::uint8_t;

namespace {

constexpr std::uint32_t kMagic = 0x524C4543;  // 'R''L''E''C'
constexpr std::uint32_t kVersion = 1;

struct Options {
  enum class Mode { Compress, Decompress };
  Mode mode = Mode::Compress;
  std::string inputPath;
  std::string outputPath;
  std::size_t chunkBytes = 4 * 1024 * 1024;  // 4 MiB
  unsigned threads = std::max(1u, std::thread::hardware_concurrency());
  bool compare = true;
  bool quiet = false;
};

struct TimedResult {
  std::chrono::nanoseconds elapsed{};
  std::uint64_t inputBytes = 0;
  std::uint64_t outputBytes = 0;
  bool ok = false;
  std::string error;
};

static void printUsage(const char* argv0) {
  std::cout
      << "Multithreaded RLE File Compressor\n\n"
      << "Usage:\n"
      << "  " << argv0
      << " compress   -i <in> -o <out.rlec> [--threads N] [--chunk BYTES] [--compare 0|1]\n"
      << "  " << argv0
      << " decompress -i <in.rlec> -o <out>     [--threads N] [--compare 0|1]\n\n"
      << "Notes:\n"
      << "  - RLE is simple and may expand incompressible data.\n"
      << "  - Use --compare 1 (default) to show single-thread vs multi-thread timings.\n";
}

template <class T>
static bool writePOD(std::ofstream& out, const T& v) {
  out.write(reinterpret_cast<const char*>(&v), sizeof(T));
  return static_cast<bool>(out);
}

template <class T>
static bool readPOD(std::ifstream& in, T& v) {
  in.read(reinterpret_cast<char*>(&v), sizeof(T));
  return static_cast<bool>(in);
}

static bool readFileAll(const std::string& path, std::vector<Byte>& data, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "Could not open input file: " + path;
    return false;
  }
  in.seekg(0, std::ios::end);
  std::streampos end = in.tellg();
  if (end < 0) {
    err = "Could not determine file size: " + path;
    return false;
  }
  const std::uint64_t size64 = static_cast<std::uint64_t>(end);
  if (size64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    err = "File too large for this build (size_t limit).";
    return false;
  }
  data.resize(static_cast<std::size_t>(size64));
  in.seekg(0, std::ios::beg);
  if (!data.empty()) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!in) {
    err = "Failed while reading input file: " + path;
    return false;
  }
  return true;
}

static std::vector<Byte> rleCompress(const Byte* data, std::size_t n) {
  std::vector<Byte> out;
  out.reserve(n);  // best-effort
  std::size_t i = 0;
  while (i < n) {
    const Byte v = data[i];
    std::size_t run = 1;
    while (i + run < n && data[i + run] == v && run < 255) ++run;
    out.push_back(static_cast<Byte>(run));
    out.push_back(v);
    i += run;
  }
  return out;
}

static bool rleDecompress(const Byte* data, std::size_t n, std::vector<Byte>& out, std::size_t expectedSize,
                          std::string& err) {
  out.clear();
  out.reserve(expectedSize);
  if (n % 2 != 0) {
    err = "Corrupt RLE data (odd length).";
    return false;
  }
  for (std::size_t i = 0; i < n; i += 2) {
    const std::uint32_t count = data[i];
    const Byte v = data[i + 1];
    if (count == 0) {
      err = "Corrupt RLE data (zero run).";
      return false;
    }
    if (out.size() + count > expectedSize) {
      err = "Corrupt RLE data (decoded chunk exceeds expected size).";
      return false;
    }
    out.insert(out.end(), static_cast<std::size_t>(count), v);
  }
  if (out.size() != expectedSize) {
    err = "Corrupt RLE data (decoded chunk size mismatch).";
    return false;
  }
  return true;
}

static double nsToSeconds(std::chrono::nanoseconds ns) {
  return static_cast<double>(ns.count()) / 1e9;
}

static double mbPerSec(std::uint64_t bytes, std::chrono::nanoseconds ns) {
  const double sec = nsToSeconds(ns);
  if (sec <= 0.0) return 0.0;
  const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
  return mib / sec;
}

static std::string humanBytes(std::uint64_t b) {
  const char* suffix[] = {"B", "KiB", "MiB", "GiB"};
  double v = static_cast<double>(b);
  int idx = 0;
  while (v >= 1024.0 && idx < 3) {
    v /= 1024.0;
    ++idx;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision((idx == 0) ? 0 : 2) << v << ' ' << suffix[idx];
  return oss.str();
}

static TimedResult compressToFile(const Options& opt, unsigned threads, bool multithreaded) {
  TimedResult tr{};
  std::vector<Byte> input;
  if (!readFileAll(opt.inputPath, input, tr.error)) return tr;

  tr.inputBytes = static_cast<std::uint64_t>(input.size());
  if (opt.chunkBytes == 0) {
    tr.error = "Chunk size must be > 0.";
    return tr;
  }

  const std::size_t nChunks =
      input.empty() ? 0 : ((input.size() + opt.chunkBytes - 1) / opt.chunkBytes);
  std::vector<std::uint32_t> origLens(nChunks);
  std::vector<std::vector<Byte>> compChunks(nChunks);

  auto t0 = std::chrono::steady_clock::now();

  if (!multithreaded || threads <= 1 || nChunks <= 1) {
    for (std::size_t c = 0; c < nChunks; ++c) {
      const std::size_t off = c * opt.chunkBytes;
      const std::size_t len = std::min(opt.chunkBytes, input.size() - off);
      origLens[c] = static_cast<std::uint32_t>(len);
      compChunks[c] = rleCompress(input.data() + off, len);
    }
  } else {
    std::atomic<std::size_t> next{0};
    std::mutex ioMutex;
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
      pool.emplace_back([&] {
        while (true) {
          const std::size_t c = next.fetch_add(1);
          if (c >= nChunks) break;
          const std::size_t off = c * opt.chunkBytes;
          const std::size_t len = std::min(opt.chunkBytes, input.size() - off);
          origLens[c] = static_cast<std::uint32_t>(len);
          compChunks[c] = rleCompress(input.data() + off, len);
          if (!opt.quiet && nChunks >= 64 && (c % 64 == 0)) {
            std::lock_guard<std::mutex> lk(ioMutex);
            std::cout << "  compressed chunk " << c << "/" << nChunks << "\n";
          }
        }
      });
    }
    for (auto& th : pool) th.join();
  }

  std::ofstream out(opt.outputPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    tr.error = "Could not open output file: " + opt.outputPath;
    return tr;
  }

  if (!writePOD(out, kMagic) || !writePOD(out, kVersion)) {
    tr.error = "Failed writing header.";
    return tr;
  }
  const std::uint32_t chunkBytes32 = static_cast<std::uint32_t>(
      std::min<std::size_t>(opt.chunkBytes, std::numeric_limits<std::uint32_t>::max()));
  const std::uint32_t nChunks32 = static_cast<std::uint32_t>(nChunks);
  const std::uint64_t originalSize64 = static_cast<std::uint64_t>(input.size());
  if (!writePOD(out, chunkBytes32) || !writePOD(out, nChunks32) || !writePOD(out, originalSize64)) {
    tr.error = "Failed writing header metadata.";
    return tr;
  }

  std::uint64_t totalCompressed = 0;
  for (std::size_t c = 0; c < nChunks; ++c) {
    const std::uint32_t origLen = origLens[c];
    const std::uint32_t compLen = static_cast<std::uint32_t>(compChunks[c].size());
    if (!writePOD(out, origLen) || !writePOD(out, compLen)) {
      tr.error = "Failed writing chunk metadata.";
      return tr;
    }
    if (compLen) out.write(reinterpret_cast<const char*>(compChunks[c].data()), compLen);
    if (!out) {
      tr.error = "Failed writing chunk bytes.";
      return tr;
    }
    totalCompressed += compLen;
  }

  out.flush();
  if (!out) {
    tr.error = "Failed finalizing output file.";
    return tr;
  }

  auto t1 = std::chrono::steady_clock::now();
  tr.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
  tr.outputBytes = 4 + 4 + 4 + 4 + 8 + nChunks * (4 + 4) + totalCompressed;
  tr.ok = true;
  (void)threads;
  return tr;
}

static TimedResult decompressToFile(const Options& opt, unsigned threads, bool multithreaded) {
  TimedResult tr{};
  std::ifstream in(opt.inputPath, std::ios::binary);
  if (!in) {
    tr.error = "Could not open input file: " + opt.inputPath;
    return tr;
  }

  std::uint32_t magic = 0, version = 0, chunkBytes32 = 0, nChunks32 = 0;
  std::uint64_t originalSize64 = 0;
  if (!readPOD(in, magic) || !readPOD(in, version) || !readPOD(in, chunkBytes32) || !readPOD(in, nChunks32) ||
      !readPOD(in, originalSize64)) {
    tr.error = "Failed reading header.";
    return tr;
  }
  if (magic != kMagic) {
    tr.error = "Not an RLEC file (bad magic).";
    return tr;
  }
  if (version != kVersion) {
    tr.error = "Unsupported RLEC version.";
    return tr;
  }

  const std::size_t nChunks = nChunks32;
  std::vector<std::uint32_t> origLens(nChunks);
  std::vector<std::vector<Byte>> compChunks(nChunks);
  std::uint64_t totalComp = 0;

  for (std::size_t c = 0; c < nChunks; ++c) {
    std::uint32_t origLen = 0, compLen = 0;
    if (!readPOD(in, origLen) || !readPOD(in, compLen)) {
      tr.error = "Corrupt file (chunk metadata truncated).";
      return tr;
    }
    origLens[c] = origLen;
    compChunks[c].resize(compLen);
    if (compLen) in.read(reinterpret_cast<char*>(compChunks[c].data()), compLen);
    if (!in) {
      tr.error = "Corrupt file (chunk bytes truncated).";
      return tr;
    }
    totalComp += compLen;
  }

  tr.inputBytes = 4 + 4 + 4 + 4 + 8 + nChunks * (4 + 4) + totalComp;

  std::vector<std::vector<Byte>> outChunks(nChunks);
  std::atomic<std::size_t> next{0};
  std::mutex errMutex;
  std::string workerErr;

  auto t0 = std::chrono::steady_clock::now();

  auto workOne = [&](std::size_t c) {
    std::string err;
    std::vector<Byte> decoded;
    if (!rleDecompress(compChunks[c].data(), compChunks[c].size(), decoded, origLens[c], err)) {
      std::lock_guard<std::mutex> lk(errMutex);
      if (workerErr.empty()) workerErr = "Chunk " + std::to_string(c) + ": " + err;
      return;
    }
    outChunks[c] = std::move(decoded);
  };

  if (!multithreaded || threads <= 1 || nChunks <= 1) {
    for (std::size_t c = 0; c < nChunks; ++c) {
      workOne(c);
      if (!workerErr.empty()) break;
    }
  } else {
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
      pool.emplace_back([&] {
        while (true) {
          if (!workerErr.empty()) return;
          const std::size_t c = next.fetch_add(1);
          if (c >= nChunks) return;
          workOne(c);
        }
      });
    }
    for (auto& th : pool) th.join();
  }

  if (!workerErr.empty()) {
    tr.error = workerErr;
    return tr;
  }

  std::ofstream out(opt.outputPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    tr.error = "Could not open output file: " + opt.outputPath;
    return tr;
  }

  std::uint64_t produced = 0;
  for (std::size_t c = 0; c < nChunks; ++c) {
    const auto& buf = outChunks[c];
    if (!buf.empty()) out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!out) {
      tr.error = "Failed writing output data.";
      return tr;
    }
    produced += buf.size();
  }

  out.flush();
  if (!out) {
    tr.error = "Failed finalizing output file.";
    return tr;
  }

  auto t1 = std::chrono::steady_clock::now();
  tr.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
  tr.outputBytes = produced;
  if (produced != originalSize64) {
    tr.error = "Corrupt file (overall decoded size mismatch).";
    tr.ok = false;
    return tr;
  }
  (void)chunkBytes32;
  tr.ok = true;
  return tr;
}

static bool parseUInt(const std::string& s, std::uint64_t& out) {
  if (s.empty()) return false;
  std::uint64_t v = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '9') return false;
    const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
    if (v > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
    v = v * 10 + digit;
  }
  out = v;
  return true;
}

static bool parseArgs(int argc, char** argv, Options& opt) {
  if (argc < 2) return false;
  const std::string cmd = argv[1];
  if (cmd == "compress" || cmd == "c") {
    opt.mode = Options::Mode::Compress;
  } else if (cmd == "decompress" || cmd == "d") {
    opt.mode = Options::Mode::Decompress;
  } else if (cmd == "--help" || cmd == "-h" || cmd == "help") {
    return false;
  } else {
    std::cerr << "Unknown command: " << cmd << "\n";
    return false;
  }

  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    auto needValue = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") + flag);
      }
      return argv[++i];
    };

    try {
      if (a == "-i" || a == "--input") {
        opt.inputPath = needValue(a.c_str());
      } else if (a == "-o" || a == "--output") {
        opt.outputPath = needValue(a.c_str());
      } else if (a == "--threads" || a == "-t") {
        std::uint64_t v = 0;
        if (!parseUInt(needValue(a.c_str()), v) || v == 0 || v > 1024) {
          throw std::runtime_error("Invalid --threads value");
        }
        opt.threads = static_cast<unsigned>(v);
      } else if (a == "--chunk") {
        std::uint64_t v = 0;
        if (!parseUInt(needValue(a.c_str()), v) || v == 0) throw std::runtime_error("Invalid --chunk value");
        opt.chunkBytes = static_cast<std::size_t>(v);
      } else if (a == "--compare") {
        std::uint64_t v = 0;
        if (!parseUInt(needValue(a.c_str()), v) || (v != 0 && v != 1)) {
          throw std::runtime_error("Invalid --compare value");
        }
        opt.compare = (v == 1);
      } else if (a == "--quiet" || a == "-q") {
        opt.quiet = true;
      } else {
        throw std::runtime_error("Unknown flag: " + a);
      }
    } catch (const std::exception& e) {
      std::cerr << "Argument error: " << e.what() << "\n";
      return false;
    }
  }

  if (opt.inputPath.empty() || opt.outputPath.empty()) return false;
  return true;
}

static void printResultLine(const std::string& label, const TimedResult& r, std::uint64_t processedBytes,
                            std::uint64_t outBytes) {
  std::cout << std::left << std::setw(14) << label << "  "
            << std::right << std::setw(9) << std::fixed << std::setprecision(3) << nsToSeconds(r.elapsed) << " s  "
            << std::setw(10) << std::fixed << std::setprecision(1) << mbPerSec(processedBytes, r.elapsed) << " MiB/s  "
            << "in " << humanBytes(processedBytes) << " -> out " << humanBytes(outBytes) << "\n";
}

static void printSpeedup(const TimedResult& single, const TimedResult& multi, unsigned threads) {
  const double s1 = nsToSeconds(single.elapsed);
  const double s2 = nsToSeconds(multi.elapsed);
  if (s2 <= 0.0) return;
  std::cout << "Speedup (" << threads << " threads): " << std::fixed << std::setprecision(2) << (s1 / s2) << "x\n";
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parseArgs(argc, argv, opt)) {
    printUsage(argv[0]);
    return 2;
  }

  std::cout << "=== Multithreaded RLE Tool ===\n";
  std::cout << "Input : " << opt.inputPath << "\n";
  std::cout << "Output: " << opt.outputPath << "\n";
  if (opt.mode == Options::Mode::Compress) {
    std::cout << "Mode  : compress\n";
    std::cout << "Chunk : " << opt.chunkBytes << " bytes\n";
  } else {
    std::cout << "Mode  : decompress\n";
  }
  std::cout << "Threads requested: " << opt.threads << "\n\n";

  const unsigned threads = std::max(1u, opt.threads);
  TimedResult single{};
  TimedResult multi{};

  if (opt.mode == Options::Mode::Compress) {
    if (opt.compare) {
      single = compressToFile(opt, 1, false);
      if (!single.ok) {
        std::cerr << "Compression failed: " << single.error << "\n";
        return 1;
      }
      const std::string mtOut = opt.outputPath + ".mt";
      Options opt2 = opt;
      opt2.outputPath = mtOut;
      multi = compressToFile(opt2, threads, true);
      if (!multi.ok) {
        std::cerr << "Compression (multithreaded) failed: " << multi.error << "\n";
        return 1;
      }

      std::cout << "Results (compression):\n";
      printResultLine("single-thread", single, single.inputBytes, single.outputBytes);
      printResultLine("multi-thread", multi, multi.inputBytes, multi.outputBytes);
      printSpeedup(single, multi, threads);
      std::cout << "\nMultithreaded output written to: " << mtOut << "\n";
      std::cout << "Single-thread output written to : " << opt.outputPath << "\n";
    } else {
      multi = compressToFile(opt, threads, threads > 1);
      if (!multi.ok) {
        std::cerr << "Compression failed: " << multi.error << "\n";
        return 1;
      }
      std::cout << "Results (compression):\n";
      printResultLine((threads > 1) ? "multi-thread" : "single-thread", multi, multi.inputBytes, multi.outputBytes);
    }
  } else {
    if (opt.compare) {
      single = decompressToFile(opt, 1, false);
      if (!single.ok) {
        std::cerr << "Decompression failed: " << single.error << "\n";
        return 1;
      }
      const std::string mtOut = opt.outputPath + ".mt";
      Options opt2 = opt;
      opt2.outputPath = mtOut;
      multi = decompressToFile(opt2, threads, true);
      if (!multi.ok) {
        std::cerr << "Decompression (multithreaded) failed: " << multi.error << "\n";
        return 1;
      }

      std::cout << "Results (decompression):\n";
      printResultLine("single-thread", single, single.inputBytes, single.outputBytes);
      printResultLine("multi-thread", multi, multi.inputBytes, multi.outputBytes);
      printSpeedup(single, multi, threads);
      std::cout << "\nMultithreaded output written to: " << mtOut << "\n";
      std::cout << "Single-thread output written to : " << opt.outputPath << "\n";
    } else {
      multi = decompressToFile(opt, threads, threads > 1);
      if (!multi.ok) {
        std::cerr << "Decompression failed: " << multi.error << "\n";
        return 1;
      }
      std::cout << "Results (decompression):\n";
      printResultLine((threads > 1) ? "multi-thread" : "single-thread", multi, multi.inputBytes, multi.outputBytes);
    }
  }

  return 0;
}

