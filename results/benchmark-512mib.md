# File analyzer comparison

Input: datasets/sample-512mib-seed42.log

536870912 bytes; 2390856 lines; 80113150 words. ERROR: 396775; WARN: 397549; INFO: 1196856.

Every timed run returned the same full set of counts as the buffered reference.

| Method | Requested/used workers | Median (s) | IQR (s) | MiB/s | Speedup vs buffered | Scaling vs mapped | Median peak working set (MiB) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| buffered | 1/1 | 0.622653 | 0.026291 | 822.29 | 1.00x | n/a | 8.02 |
| mapped | 1/1 | 0.714743 | 0.064600 | 716.34 | 0.87x | 1.00x | 518.99 |
| mapped-parallel | 1/1 | 0.721056 | 0.022381 | 710.07 | 0.86x | 0.99x | 518.99 |
| mapped-parallel | 2/2 | 0.394419 | 0.028435 | 1298.11 | 1.58x | 1.81x | 519.04 |
| mapped-parallel | 4/4 | 0.236749 | 0.033821 | 2162.63 | 2.63x | 3.02x | 519.04 |
| mapped-parallel | 8/8 | 0.197601 | 0.014852 | 2591.09 | 3.15x | 3.62x | 519.06 |
| mapped-parallel | 12/12 | 0.176214 | 0.017976 | 2905.56 | 3.53x | 4.06x | 519.09 |

## Protocol and interpretation

Each configuration ran 7 times in fresh processes. Configurations were shuffled within each round. The file was completely pre-read outside the trial process before each run; cache residency is not guaranteed. This experiment does not claim cold-cache or raw disk throughput.

Timing includes file validation/open, mapping, partitioning, worker startup/join, merge, and close/unmap. It excludes process startup, pre-read, metrics queries, and report output. IQR uses linearly interpolated 25th and 75th percentiles.

Peak working set is a lifetime maximum per fresh process, including its runtime baseline and resident mapped pages; it is not heap allocation or incremental memory above baseline. Page faults combine soft and hard faults. CPU timing may be coarse for short trials. Speedup below 1 means the method was slower than buffered reading.

Line-aligned partitioning may use fewer workers for very long or very few lines. These measurements apply to this dataset and machine; they do not establish that mapping is always faster.

## Environment

| Field | Value |
| --- | --- |
| timestamp_utc | 2026-10-05T23:46:28Z |
| input | datasets/sample-512mib-seed42.log |
| file_bytes | 536870912 |
| file_mtime_ticks | -4646420019000000000 |
| cpu | 12th Gen Intel(R) Core(TM) i5-12450H |
| logical_processors | 12 |
| physical_ram_bytes | 16802910208 |
| available_ram_bytes_at_start | 2482343936 |
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
