# High-Performance Memory-Mapped Concurrent File Analyzer

A C++20 Windows application that compares normal buffered reading with read-only memory mapping and concurrent mapped analysis. It counts bytes, lines, words, and exact log keywords using the same scanner in every mode.

## Build and run

The supplied PowerShell build script uses a 64-bit MinGW GCC compiler (`g++`) on PATH. No third-party C++ libraries are required. Run from the project folder:

```powershell
.\scripts\build.ps1 -RunTests
.\scripts\demo.ps1
```

The demo reuses or generates a 64 MiB seeded log under `%LOCALAPPDATA%\FileAnalyzer\OS_project_2026\datasets` and saves a measured comparison under `results`. A MinGW build is linked statically with its C++ and threading runtime, so the resulting executable needs only standard Windows DLLs. To run individual commands:

```powershell
.\build\file_analyzer.exe --help
.\build\file_analyzer.exe generate --output "$env:TEMP\analyzer-sample.log" --size-mib 64 --seed 42
.\build\file_analyzer.exe analyze "$env:TEMP\analyzer-sample.log" --mode buffered
.\build\file_analyzer.exe analyze "$env:TEMP\analyzer-sample.log" --mode mapped-parallel --threads 4
.\build\file_analyzer.exe benchmark "$env:TEMP\analyzer-sample.log" --threads 1,2,4,8 --runs 7 --output results\benchmark.csv
```

Generation refuses to overwrite existing files. Choose another output path or explicitly add `--force` to regenerate a dataset. File paths containing spaces or non-ASCII characters are supported.

If CMake is installed, an alternative build works with MinGW or Visual Studio's 64-bit C++ toolchain:

```powershell
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

For a single-configuration generator such as Ninja or MinGW Makefiles, add `-DCMAKE_BUILD_TYPE=Release` to the configure command. Visual Studio executables are typically placed in `build-cmake\Release`.

## Analysis modes

| Mode | Access | Workers | Role |
| --- | --- | --- | --- |
| `buffered` | Binary `std::ifstream`, reusable 1 MiB buffer | 1 | Normal baseline |
| `mapped` | Windows read-only whole-file mapping | 1 | Mapping control |
| `mapped-parallel` | One mapping, disjoint line-aligned ranges | Requested count, or fewer if there are too few useful ranges | Chosen optimized method |

`analyze` defaults to `mapped-parallel` and the machine's logical processor count. `benchmark` always includes buffered and single-threaded mapped controls, then the requested mapped-parallel configurations. `--threads` accepts 1 through 256; benchmarking accepts a comma-separated list. `--buffer-kib` accepts 1 through 65536 (default 1024).

## Counting rules

- Bytes count every byte, including CRLF, NUL, and non-ASCII bytes.
- Words are maximal runs of ASCII letters, digits, or underscore. Other bytes are separators.
- Keywords are complete, case-sensitive words: `ERROR`, `WARN`, and `INFO`. `ERRORS`, `error`, and `WARN_message` do not match.
- Lines are LF-delimited; CRLF is supported. A final nonempty line without LF counts. An empty file has zero lines.
- The scanner preserves token state across buffered reads and never allocates individual words.

This is raw text/log analysis, not CSV parsing, Unicode word segmentation, substring search, or document extraction. Inputs must be local regular files and must remain unchanged while analyzing or benchmarking. The mapping holds a read handle that denies ordinary writers; file size and modification time are checked between benchmark trials. These checks are not a cryptographic proof of immutability.

## Concurrency and resource ownership

One RAII object owns the Windows file handle, mapping handle, and read-only view. Empty files bypass mapping. File size and counters are 64-bit. The partitioner moves cuts to complete line boundaries and creates contiguous nonoverlapping ranges. Each worker owns its scanner and counters; workers only read the shared mapping. Joining occurs before reduction and unmapping. No mutex or shared-counter updates occur in the scanning loop.

For a file containing one very long line, only one effective worker may be used. Both the requested and effective counts are displayed. This implementation uses whole-file mapping on 64-bit Windows; mapping failures are reported, rather than silently switching methods. Linux support and windowed mappings are future extensions.

## Benchmark protocol

Every trial runs one method in a fresh child process. The parent performs a complete untimed pre-read before each child, avoiding pre-read memory contaminating the child's lifetime peak. Configuration order is shuffled within each round with `--order-seed` (default 42). The input is generated before benchmarking. The default is seven observations per configuration.

Measured elapsed time includes input validation/open, allocation/mapping, range partitioning, worker creation/join, scanning, merge, and close/unmap. It excludes child-process startup, cache pre-read, metrics queries, progress output, and report writing.

Every timed result is compared with the buffered reference's entire set of analysis counts; any mismatch fails the benchmark. The independent correctness tests separately guard against bugs shared by all readers.

- Median elapsed time and IQR (75th minus 25th percentile) use linear interpolation between sorted observations.
- Throughput is `file bytes / 1,048,576 / median seconds`, in MiB/s; it describes analysis throughput, not necessarily physical disk speed.
- Speedup is `buffered median / method median`; below 1 means a slowdown.
- Mapped scaling is `single-threaded mapped median / method median`.
- CPU time is the process user + kernel delta across all its threads. Short runs can show coarse or zero CPU times.
- Peak working set is each fresh process's lifetime peak, including its runtime baseline and resident mapped pages. It is not heap usage or memory above baseline.
- Page-fault delta combines soft and hard Windows faults. It does not count disk reads directly.

Cache pre-read attempts to warm the filesystem cache; residency is not guaranteed, and no cold-cache claim is made. Metadata labels memory pressure when the file exceeds available RAM at the start. Keep benchmark files outside actively synced folders and close competing heavy workloads when collecting final project results.

## Reports

For `--output results\benchmark.csv`, the application creates:

| File | Contents |
| --- | --- |
| `benchmark.csv` | Individual trials, counts, timing, CPU, working set, and page faults |
| `benchmark.summary.csv` | Median/IQR, throughput, speedup, scaling, and time reduction |
| `benchmark.md` | Readable comparison table and protocol |
| `benchmark.metadata.txt` | CPU, RAM, Windows build, compiler, file path/size/time, filesystem, cache policy |

Reports at the selected output location are replaced on a successful new benchmark. The application rejects any report path that would overwrite the input file. Storage model/type should be recorded separately; filesystem metadata is collected automatically.

See [the measured comparison](results/COMPARISON.md) for the first verified 1, 64, and 512 MiB experiments and exported graphs. These graphs are snapshots of those observations; the C++ program exports the tables and CSV data for new runs.

## Verification

`analyzer_tests` compares all modes with an independent string-search reference and manually known answers. It covers empty input, LF/CRLF, punctuation, embedded NUL, UTF-8 separators, mixed case, exact keyword matching, small read buffers, a token crossing the 1 MiB read boundary, an oversized word/line, more workers than useful ranges, randomized text, deterministic generation, denied mapping access, and invalid API arguments. Partition checks verify complete coverage, no overlap, and complete line boundaries. `scripts/smoke-tests.ps1` also tests the CLI, subprocess trials with a Unicode path containing spaces and an ampersand, CSV exports, empty-file benchmarks, invalid options, and protection against overwriting input. `build.ps1 -RunTests` runs both suites.

The app also rejects unknown/duplicate CLI options, invalid counts, missing inputs, and report/input path collisions. Re-run the correctness checks after modifying scanner or partition logic. Use Release builds for measurements.

## Source map

| Location | Responsibility |
| --- | --- |
| `src/scanner.cpp` | Shared incremental byte/token/line scanner |
| `src/analyzer.cpp` | Buffered and mapped analysis; partitioning and workers |
| `src/mapped_file_windows.cpp` | RAII Windows mapping |
| `src/metrics_windows.cpp` | Elapsed, CPU, resident-memory, and fault metrics |
| `src/dataset_generator.cpp` | Deterministic exact-size log generation |
| `src/benchmark.cpp` | Fresh trial processes, pre-read, shuffled rounds, statistics |
| `src/report.cpp` | Console, CSV, and Markdown reports |
| `src/main.cpp` | Unicode-aware command-line interface and validation |
| `tests/correctness_tests.cpp` | Independent correctness and boundary tests |

The original architecture and milestones are in [PROJECT_PLAN.md](PROJECT_PLAN.md).

## OS concepts and references

The project demonstrates virtual address spaces, demand paging, filesystem caching, read-only sharing, thread scheduling, private worker state, and the serial reduction step. File mapping can reduce explicit buffering/copying, but mapping overhead, page faults, disk access, and memory bandwidth can offset that benefit; the comparison should establish what happens on the measured workload.

API references: [Windows file mapping](https://learn.microsoft.com/windows/win32/memory/file-mapping), [MapViewOfFile](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile), [process CPU times](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes), [memory counters](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex).
