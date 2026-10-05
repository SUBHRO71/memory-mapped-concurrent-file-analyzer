# File analyzer comparison

Input: datasets/sample-1mib-seed42.log

1048576 bytes; 4679 lines; 156464 words. ERROR: 772; WARN: 752; INFO: 2429.

Every timed run returned the same full set of counts as the buffered reference.

| Method | Requested/used workers | Median (s) | IQR (s) | MiB/s | Speedup vs buffered | Scaling vs mapped | Median peak working set (MiB) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| buffered | 1/1 | 0.001783 | 0.000066 | 561.01 | 1.00x | n/a | 8.02 |
| mapped | 1/1 | 0.001535 | 0.000126 | 651.38 | 1.16x | 1.00x | 7.99 |
| mapped-parallel | 1/1 | 0.001530 | 0.000142 | 653.42 | 1.16x | 1.00x | 7.99 |
| mapped-parallel | 2/2 | 0.001149 | 0.000221 | 870.17 | 1.55x | 1.34x | 8.04 |
| mapped-parallel | 4/4 | 0.000950 | 0.000073 | 1052.52 | 1.88x | 1.62x | 8.04 |
| mapped-parallel | 8/8 | 0.001085 | 0.000071 | 921.32 | 1.64x | 1.41x | 8.07 |
| mapped-parallel | 12/12 | 0.001186 | 0.000052 | 842.89 | 1.50x | 1.29x | 8.10 |

## Protocol and interpretation

Each configuration ran 7 times in fresh processes. Configurations were shuffled within each round. The file was completely pre-read outside the trial process before each run; cache residency is not guaranteed. This experiment does not claim cold-cache or raw disk throughput.

Timing includes file validation/open, mapping, partitioning, worker startup/join, merge, and close/unmap. It excludes process startup, pre-read, metrics queries, and report output. IQR uses linearly interpolated 25th and 75th percentiles.

Peak working set is a lifetime maximum per fresh process, including its runtime baseline and resident mapped pages; it is not heap allocation or incremental memory above baseline. Page faults combine soft and hard faults. CPU timing may be coarse for short trials. Speedup below 1 means the method was slower than buffered reading.

Line-aligned partitioning may use fewer workers for very long or very few lines. These measurements apply to this dataset and machine; they do not establish that mapping is always faster.

## Environment

| Field | Value |
| --- | --- |
| timestamp_utc | 2026-10-05T23:46:21Z |
| input | datasets/sample-1mib-seed42.log |
| file_bytes | 1048576 |
| file_mtime_ticks | -4646420020000000000 |
| cpu | 12th Gen Intel(R) Core(TM) i5-12450H |
| logical_processors | 12 |
| physical_ram_bytes | 16802910208 |
| available_ram_bytes_at_start | 2383437824 |
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
