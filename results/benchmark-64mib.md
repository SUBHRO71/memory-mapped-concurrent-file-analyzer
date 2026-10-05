# File analyzer comparison

Input: datasets/sample-64mib-seed42.log

67108864 bytes; 298754 lines; 10014231 words. ERROR: 49630; WARN: 49405; INFO: 149472.

Every timed run returned the same full set of counts as the buffered reference.

| Method | Requested/used workers | Median (s) | IQR (s) | MiB/s | Speedup vs buffered | Scaling vs mapped | Median peak working set (MiB) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| buffered | 1/1 | 0.081115 | 0.002755 | 789.00 | 1.00x | n/a | 8.03 |
| mapped | 1/1 | 0.096250 | 0.002174 | 664.94 | 0.84x | 1.00x | 70.99 |
| mapped-parallel | 1/1 | 0.094441 | 0.007788 | 677.67 | 0.86x | 1.02x | 71.00 |
| mapped-parallel | 2/2 | 0.053426 | 0.003436 | 1197.92 | 1.52x | 1.80x | 71.05 |
| mapped-parallel | 4/4 | 0.031521 | 0.002972 | 2030.39 | 2.57x | 3.05x | 71.04 |
| mapped-parallel | 8/8 | 0.028666 | 0.001486 | 2232.59 | 2.83x | 3.36x | 71.07 |
| mapped-parallel | 12/12 | 0.026460 | 0.001403 | 2418.77 | 3.07x | 3.64x | 71.09 |

## Protocol and interpretation

Each configuration ran 7 times in fresh processes. Configurations were shuffled within each round. The file was completely pre-read outside the trial process before each run; cache residency is not guaranteed. This experiment does not claim cold-cache or raw disk throughput.

Timing includes file validation/open, mapping, partitioning, worker startup/join, merge, and close/unmap. It excludes process startup, pre-read, metrics queries, and report output. IQR uses linearly interpolated 25th and 75th percentiles.

Peak working set is a lifetime maximum per fresh process, including its runtime baseline and resident mapped pages; it is not heap allocation or incremental memory above baseline. Page faults combine soft and hard faults. CPU timing may be coarse for short trials. Speedup below 1 means the method was slower than buffered reading.

Line-aligned partitioning may use fewer workers for very long or very few lines. These measurements apply to this dataset and machine; they do not establish that mapping is always faster.

## Environment

| Field | Value |
| --- | --- |
| timestamp_utc | 2026-10-05T23:46:23Z |
| input | datasets/sample-64mib-seed42.log |
| file_bytes | 67108864 |
| file_mtime_ticks | -4646420163000000000 |
| cpu | 12th Gen Intel(R) Core(TM) i5-12450H |
| logical_processors | 12 |
| physical_ram_bytes | 16802910208 |
| available_ram_bytes_at_start | 2417111040 |
| os | Windows build 26300 |
| compiler | GCC 15.2.0 |
| build | Release (NDEBUG) |
| buffer_bytes | 1048576 |
| runs_per_configuration | 7 |
| order_seed | 42 |
| cache_policy | pre-read before each trial; residency not guaranteed; no cold-cache claim |
| volume_root | C:\\ |
| filesystem | NTFS |
| storage_device | Record SSD/HDD/model separately; not automatically detected |
| memory_metric | Lifetime peak working set of a fresh process; includes runtime baseline and resident mapped pages |
| page_fault_metric | Windows total process page-fault delta (soft and hard combined) |
| timer_scope | Input validation/open through close/unmap and worker join; excludes process startup, pre-read, metrics queries, output |
