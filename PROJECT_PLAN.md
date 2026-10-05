# High-Performance Memory-Mapped Concurrent File Analyzer

## Project objective

Build a C++ application that analyzes large, read-only text and log files and compares conventional buffered file reading with concurrent memory-mapped analysis. Both methods must produce exactly the same analysis results. The project should measure when the optimized method helps and explain cases where it is slower.

Proposed scope: one local, uncompressed, immutable file per analysis; a 64-bit C++20 application; Windows first, with an optional Linux backend. This is a proposed implementation plan, not an implemented application or measured performance report.

## 1. What the analyzer will do

For each input file, calculate in one pass:

- Total bytes, using 64-bit counters.
- Logical line count.
- Word count.
- Exact occurrences of the tokens `ERROR`, `WARN`, and `INFO`.

Define the rules before implementing either reader:

- A word is a maximal run of ASCII letters, digits, or underscore. Punctuation and other bytes separate words.
- Keyword matching is case-sensitive and matches complete words: `ERROR` counts, `ERRORS` does not.
- A logical line ends at LF (`\n`); CRLF is accepted as well. Empty file: zero lines. A nonempty final line without LF still counts as a line.
- Non-ASCII bytes contribute to byte counts but act as word separators. Unicode language-aware tokenization is outside the initial scope.
- Input is raw text. CSV quoting, compressed files, PDF extraction, and arbitrary substring search are outside the initial scope.

This gives a useful log-analysis demonstration without making the workload too complicated to validate. Token-heavy files also provide enough CPU work to investigate concurrency.

## 2. Methods to compare

| Mode | File access | Workers | Purpose |
| --- | --- | --- | --- |
| `buffered` | `std::ifstream` opened in binary mode, reading fixed-size blocks | 1 | Normal baseline |
| `mapped` | Read-only memory mapping | 1 | Measure mapping independently of threading |
| `mapped-parallel` | One read-only mapping, divided into independent ranges | Configurable | Chosen optimized method |

Use a reasonable buffered baseline: start with a reusable 1 MiB buffer, document its size, and keep it fixed for the main experiment. Do not read one character per system call or add extra work to the baseline.

All modes use the same token rules and scanner logic. A scanner supports incremental input, so words split across buffered reads are counted correctly. Mapped workers begin at line boundaries and finish their own scanner state at the end of their range.

An optional fourth mode, buffered parallel reading, can be added later for a fuller two-by-two experiment. The three modes above are sufficient for the initial deliverable and distinguish the mapping change at one thread from the threading change under mapped access.

## 3. How the selected method works

```text
Input file
    |
    +-- Buffered reader -----------------> One scanner ----------+
    |                                                           |
    +-- Read-only mapping ---------------> One scanner ----------+--> Analysis result
    |                                                           |    + benchmark metrics
    +-- Read-only mapping --> Range partitioner --> Workers -----+
                                               --> Merge results
```

For `mapped-parallel`:

1. Open the file for reading, determine its size, and create one read-only mapping.
2. Handle an empty file directly without attempting to map it.
3. Choose a worker count. Test 1, 2, 4, and higher available counts; handle `hardware_concurrency()` returning zero by defaulting to one.
4. Calculate approximate equal-byte boundaries. Move each interior boundary to the beginning of the next complete line, unless it already starts a line. Use one shared, monotonically increasing list of boundaries.
5. Give each worker a disjoint range `[begin, end)`. Skip empty ranges. Do not count a line or token in two workers.
6. Each worker runs the scanner and maintains private counters. No shared counter updates or locks are needed in the scanning loop.
7. Join every worker, then sum their results.
8. Unmap the view and close resources only after workers have finished.

Use a small private token state rather than allocating strings for every word. Keyword recognition only needs to retain enough characters to distinguish the configured keywords; oversized tokens still count as words.

Line-boundary partitioning makes correctness easier. A very long single line can produce poor load balance; report this behavior explicitly and permit a sequential fallback. More complex byte-level partitioning is an extension, not a requirement for version one.

Mapping makes file bytes accessible through virtual memory; it does not mean the whole file is immediately loaded into physical RAM. Windows provides native file-mapping APIs, including read-only views. [Microsoft file mapping documentation](https://learn.microsoft.com/windows/win32/memory/file-mapping), [MapViewOfFile](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile).

## 4. C++ implementation choices

- Language: C++20, built as a 64-bit executable.
- Build: CMake, with Release configuration for measurements.
- Concurrency: `std::thread` or `std::jthread`; one worker per range for the initial version.
- Timing: `std::chrono::steady_clock`.
- Counters and file sizes: 64-bit integers, with checked conversion to address-size types.
- Ownership: RAII wrappers for file handles and mappings; cleanup on errors.
- Interface: command-line application with clear help, analysis output, and a comparison table.

Windows mapping backend: `CreateFileW`, `GetFileSizeEx`, `CreateFileMappingW(PAGE_READONLY)`, `MapViewOfFile(FILE_MAP_READ)`, `UnmapViewOfFile`, and `CloseHandle`. Use compatible sharing rules to deny ordinary writers while the input is open, and document that input files must remain unchanged.

Optional Linux backend: `open`, `fstat`, `mmap(PROT_READ, MAP_PRIVATE)`, `munmap`, and `close`, behind the same wrapper. Do not benchmark Windows and Linux against each other as if only the algorithm had changed. [Linux mmap documentation](https://man7.org/linux/man-pages/man2/mmap.2.html).

Start with whole-file mapping for the chosen test sizes on a 64-bit system. If mapping fails, return a clear error or explicitly labeled buffered fallback. Windowed mapping is a later extension; mapped offsets must respect the operating system's alignment requirements. [MapViewOfFile alignment requirements](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile).

## 5. Proposed source structure

```text
OS_project_2026/
  CMakeLists.txt
  README.md
  PROJECT_PLAN.md
  include/analyzer/
    analysis_result.hpp
    scanner.hpp
    mapped_file.hpp
    partitioner.hpp
    metrics.hpp
  src/
    main.cpp
    scanner.cpp
    buffered_analyzer.cpp
    mapped_analyzer.cpp
    parallel_analyzer.cpp
    mapped_file_windows.cpp
    metrics_windows.cpp
    benchmark.cpp
    dataset_generator.cpp
  tests/
    correctness_tests.cpp
  results/
    benchmark.csv
    comparison.md
```

The `results` files will contain real measurements once the program exists; they should not be populated with invented benchmark numbers. Optional Linux implementation files can be added when that platform becomes part of the deliverable.

## 6. Proposed commands and demonstration

The following commands describe the planned interface; they are not available yet.

```text
file_analyzer generate --output sample.log --size-mib 512 --seed 42
file_analyzer analyze sample.log --mode buffered
file_analyzer analyze sample.log --mode mapped
file_analyzer analyze sample.log --mode mapped-parallel --threads 4
file_analyzer benchmark sample.log --threads 1,2,4 --runs 7 --output results/benchmark.csv
```

A benchmark should print a table such as this, populated only with measured values:

| Method | Workers | Median time (s) | Throughput (MiB/s) | Speedup | Peak working set (MiB) | Results match |
| --- | --- | --- | --- | --- | --- | --- |
| Buffered | 1 | measured | measured | 1.00x | measured | reference |
| Mapped | 1 | measured | measured | calculated | measured | yes/no |
| Mapped parallel | 4 | measured | measured | calculated | measured | yes/no |

Show the counts as well as the performance table, so the audience can verify that every method performs the same task. A result mismatch must fail the benchmark and invalidate its performance comparison.

## 7. Measurements and formulas

Record these primary metrics:

- End-to-end analysis time: from immediately before opening the input through closing/unmapping after the final merge. Include mapping, partitioning, worker creation, and joining. Exclude data generation, process startup, console output, and report writing.
- Throughput: `(file bytes / 1,048,576) / elapsed seconds`, reported as MiB/s.
- Speedup: `median buffered time / median optimized time` for the same file and cache condition.
- Time reduction: `100 * (baseline time - optimized time) / baseline time`; negative values mean a slowdown.
- Thread scaling: `median mapped time at 1 worker / median mapped time at P workers`. Keep this distinct from speedup against the buffered baseline.
- Process CPU time: user plus kernel time over the analysis interval.
- Peak resident memory: Windows peak working set; Linux peak RSS if the Linux backend is built.

For additional OS evidence, collect process page-fault deltas. Windows exposes a total count; Linux provides separate minor and major counts. These fields are platform-specific and should not be presented as identical measurements. Windows CPU times sum execution across process threads and can exceed elapsed wall time. [Windows process CPU times](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes), [Windows memory counters](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex), [Linux resource usage](https://man7.org/linux/man-pages/man2/getrusage.2.html).

Do not claim mapping uses less RAM solely because it avoids a large heap buffer. Resident mapped pages consume memory too. Memory comparisons must use the same clearly labeled metric and include the mapping's resident pages.

## 8. Fair benchmark protocol

1. Generate deterministic text/log files before timing. Use a fixed seed and record the exact file size and dataset identifier.
2. Use approximately 1 MiB, 64 MiB, 512 MiB, and 1 GiB files, subject to available disk space. Include both short log lines and variable-length lines.
3. Verify every mode against independently checked small fixtures before doing large performance runs.
4. Benchmark the same immutable file, scanner, keyword set, compiler optimization, and machine. Record CPU, RAM, storage type, OS, compiler, buffer size, file size, and thread count.
5. For the main experiment, use explicitly warmed filesystem-cache runs. Perform an untimed complete read before each measured run when the dataset fits available memory. A fresh process does not clear the filesystem cache.
6. Run each configuration seven times in a fresh child process per trial. Randomize configuration order in each round. Report the median and interquartile range, and preserve individual results in CSV.
7. Each trial process executes one method, so lifetime peak-memory measurements are not contaminated by an earlier method in the same process.
8. If a dataset cannot remain cached, label it as memory-constrained or uncontrolled cache rather than claiming it is fully warm. Cold-cache experiments are optional and require a separate, documented reset procedure applied before every trial; an ordinary first run is not proof of a cold cache.
9. Keep files on a local drive outside actively synced folders during performance runs. Record the benchmark path; project source can stay in this workspace.
10. Do not print progress, generate files, run competing configurations simultaneously, or write reports inside the measured interval.

The optimized method may lose on small inputs because setup and thread creation dominate. Disk speed, filesystem caching, line imbalance, page faults, and memory bandwidth can limit improvement on larger inputs. These are hypotheses to investigate, not promised results.

## 9. Correctness tests

Use a small independent reference implementation or manually computed fixtures, then verify all analysis fields against them:

- Empty file; one byte; a file containing only separators.
- LF and CRLF, blank lines, and a final line without a newline.
- Exact keyword matches, punctuation, repeated keywords, mixed case, and longer words containing keyword text.
- A word split across the buffered reader's block boundary.
- A nominal worker boundary inside a word or a CRLF pair.
- One extremely long line, including a line longer than the read buffer.
- More requested workers than useful ranges; repeated empty ranges after alignment.
- Input larger than a 32-bit file-size counter, if resources permit.
- Missing file, denied access, mapping failure, and invalid thread arguments.

Cross-method agreement alone is insufficient if the shared scanner contains a bug; the independent expected results are essential. Run correctness checks before accepting any benchmark. Use sanitizers where the selected compiler supports them, outside timed Release measurements.

## 10. Implementation milestones

| Stage | Approximate effort | Deliverable |
| --- | --- | --- |
| Define rules and skeleton | 1 day | CMake build, CLI contract, result structure, small fixtures |
| Implement baseline | 1-2 days | Buffered reader and correct incremental scanner |
| Add mapping | 1-2 days | RAII Windows mapping and single-threaded mapped analysis |
| Add concurrency | 2 days | Safe partitioning, private worker results, merge |
| Add experiments | 2 days | Dataset generator, fresh-process benchmark runner, metrics, CSV |
| Validate and present | 2 days | Correctness suite, actual measurements, charts, README and report |

Estimate: approximately 9-11 working days for the initial Windows version, depending on C++ familiarity. Platform portability or a graphical interface adds time.

## 11. Final presentation

Demonstrate one small file with known answers, then analyze one large log file using all three modes. Show:

1. Identical byte, line, word, and keyword counts.
2. Normal versus optimized elapsed time and throughput.
3. The mapped single-threaded control explaining the effect of mapping.
4. A worker-count graph explaining the effect of concurrency.
5. Peak resident memory and page-fault observations, with their limitations.

Include three graphs in the report: file size versus time, workers versus mapped throughput, and method versus peak resident memory. Add a brief explanation of virtual memory, demand paging, filesystem caching, thread scheduling, private worker state, and the serial merge step.

## 12. Completion criteria and optional extensions

The initial project is complete when the C++ app builds reproducibly, all methods match independently validated answers, resources are released correctly, the benchmark protocol is reproducible, and the report contains real comparisons and a reasoned explanation of the observed limits. There is no required invented speedup target.

Extensions after this version: Linux backend, buffered parallel control, windowed mappings, a reusable worker pool for batches of files, Unicode tokenization, SIMD scanning, or a graphical dashboard. Add one extension at a time so its effect can be measured separately.
