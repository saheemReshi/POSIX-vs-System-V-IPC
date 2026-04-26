# IPC Benchmark Analysis v2

## Dataset Summary
- Total rows: 174
- Total unique (mechanism, benchmark, run_id): 24

## Coverage Summary
| mechanism | benchmark | rows | unique_runs | placements | msg_sizes |
|---|---:|---:|---:|---:|---:|
| mq_posix | fanin_n1 | 3 | 1 | 1 | 3 |
| mq_sysv | fanin_n1 | 3 | 1 | 1 | 3 |
| shm_posix | fanin_n1 | 0 | 0 | 0 | 0 |
| shm_sysv | fanin_n1 | 0 | 0 | 0 | 0 |
| mq_posix | fanin_n16 | 3 | 1 | 1 | 3 |
| mq_sysv | fanin_n16 | 3 | 1 | 1 | 3 |
| shm_posix | fanin_n16 | 0 | 0 | 0 | 0 |
| shm_sysv | fanin_n16 | 0 | 0 | 0 | 0 |
| mq_posix | fanin_n2 | 3 | 1 | 1 | 3 |
| mq_sysv | fanin_n2 | 3 | 1 | 1 | 3 |
| shm_posix | fanin_n2 | 0 | 0 | 0 | 0 |
| shm_sysv | fanin_n2 | 0 | 0 | 0 | 0 |
| mq_posix | fanin_n4 | 3 | 1 | 1 | 3 |
| mq_sysv | fanin_n4 | 3 | 1 | 1 | 3 |
| shm_posix | fanin_n4 | 0 | 0 | 0 | 0 |
| shm_sysv | fanin_n4 | 0 | 0 | 0 | 0 |
| mq_posix | fanin_n8 | 3 | 1 | 1 | 3 |
| mq_sysv | fanin_n8 | 3 | 1 | 1 | 3 |
| shm_posix | fanin_n8 | 0 | 0 | 0 | 0 |
| shm_sysv | fanin_n8 | 0 | 0 | 0 | 0 |
| mq_posix | pairs_n1 | 3 | 1 | 1 | 3 |
| mq_sysv | pairs_n1 | 3 | 1 | 1 | 3 |
| shm_posix | pairs_n1 | 0 | 0 | 0 | 0 |
| shm_sysv | pairs_n1 | 0 | 0 | 0 | 0 |
| mq_posix | pairs_n2 | 3 | 1 | 1 | 3 |
| mq_sysv | pairs_n2 | 3 | 1 | 1 | 3 |
| shm_posix | pairs_n2 | 0 | 0 | 0 | 0 |
| shm_sysv | pairs_n2 | 0 | 0 | 0 | 0 |
| mq_posix | pairs_n4 | 3 | 1 | 1 | 3 |
| mq_sysv | pairs_n4 | 3 | 1 | 1 | 3 |
| shm_posix | pairs_n4 | 0 | 0 | 0 | 0 |
| shm_sysv | pairs_n4 | 0 | 0 | 0 | 0 |
| mq_posix | pairs_n8 | 3 | 1 | 1 | 3 |
| mq_sysv | pairs_n8 | 3 | 1 | 1 | 3 |
| shm_posix | pairs_n8 | 0 | 0 | 0 | 0 |
| shm_sysv | pairs_n8 | 0 | 0 | 0 | 0 |
| mq_posix | pingpong | 12 | 1 | 2 | 6 |
| mq_sysv | pingpong | 12 | 1 | 2 | 6 |
| shm_posix | pingpong | 12 | 1 | 2 | 6 |
| shm_sysv | pingpong | 12 | 1 | 2 | 6 |
| mq_posix | throughput | 36 | 1 | 2 | 6 |
| mq_sysv | throughput | 36 | 1 | 2 | 6 |
| shm_posix | throughput | 0 | 0 | 0 | 0 |
| shm_sysv | throughput | 0 | 0 | 0 | 0 |

## Key Observations
- At 64B same_core pingpong, POSIX MQ p99 is 9.8% higher than SYSV MQ (2992 ns vs 2726 ns).
- Peak same_core throughput favors POSIX MQ by 6.7% (9480.4 MB/s vs 8884.7 MB/s).
- At high fan-in load (n_procs=17, 64B), POSIX throughput is 618.8% higher than SYSV. At high parallel-pairs load (n_procs=16, 64B), POSIX p99 latency is 2.0% lower than SYSV.

## Notes
- Plots are split to avoid crowding and keep comparisons readable.
- Message-size axes use log scale where size sweeps are shown.
- Error bars are included only when repeated-run variance can be shown without clutter.

