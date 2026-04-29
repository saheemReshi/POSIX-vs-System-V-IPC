# POSIX vs System V IPC — Analysis Report

- Total rows: 10200
- Mechanisms present: ['mq_posix', 'mq_sysv', 'shm_posix', 'shm_sysv']
- Benchmarks present: ['fanin_n1', 'fanin_n16', 'fanin_n2', 'fanin_n4', 'fanin_n8', 'pairs_n1', 'pairs_n2', 'pairs_n4', 'pairs_n8', 'pingpong', 'throughput']
- Placements present: ['diff_l3', 'fanin_n1', 'fanin_n16', 'fanin_n2', 'fanin_n4', 'fanin_n8', 'pairs_n1', 'pairs_n2', 'pairs_n4', 'pairs_n8', 'same_core', 'same_l3']

## Headline comparisons (significant cells, p < 0.05)

- **MQ fanin** (64B, N=1): ratio sysv/posix = 0.96× [0.96–0.97], MW p=6.0e-07, Cliff δ=0.93 (large), winner=mq_posix
- **MQ fanin** (64B, N=2): ratio sysv/posix = 0.85× [0.85–0.87], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (64B, N=4): ratio sysv/posix = 0.75× [0.74–0.75], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (64B, N=8): ratio sysv/posix = 0.40× [0.40–0.40], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (64B, N=16): ratio sysv/posix = 0.18× [0.18–0.18], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (256B, N=1): ratio sysv/posix = 1.03× [1.02–1.04], MW p=5.9e-06, Cliff δ=-0.84 (large), winner=mq_sysv
- **MQ fanin** (256B, N=2): ratio sysv/posix = 0.87× [0.85–0.90], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (256B, N=4): ratio sysv/posix = 0.75× [0.75–0.75], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (256B, N=8): ratio sysv/posix = 0.41× [0.41–0.42], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (256B, N=16): ratio sysv/posix = 0.18× [0.18–0.18], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (1024B, N=1): ratio sysv/posix = 1.02× [1.00–1.02], MW p=2.1e-03, Cliff δ=-0.57 (large), winner=mq_sysv
- **MQ fanin** (1024B, N=2): ratio sysv/posix = 0.89× [0.89–0.92], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (1024B, N=4): ratio sysv/posix = 0.74× [0.74–0.74], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (1024B, N=8): ratio sysv/posix = 0.45× [0.45–0.45], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ fanin** (1024B, N=16): ratio sysv/posix = 0.20× [0.19–0.20], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ pairs** (64B, N=1): ratio sysv/posix = 1.02× [1.02–1.03], MW p=1.4e-05, Cliff δ=-0.80 (large), winner=mq_posix
- **MQ pairs** (64B, N=1): ratio sysv/posix = 1.02× [1.02–1.03], MW p=8.4e-05, Cliff δ=-0.73 (large), winner=mq_posix
- **MQ pairs** (64B, N=2): ratio sysv/posix = 1.01× [1.01–1.02], MW p=6.5e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pairs** (64B, N=2): ratio sysv/posix = 1.02× [1.01–1.02], MW p=3.3e-05, Cliff δ=-0.77 (large), winner=mq_posix
- **MQ pairs** (64B, N=2): ratio sysv/posix = 0.22× [0.18–0.31], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_sysv
- **MQ pairs** (64B, N=4): ratio sysv/posix = 1.02× [1.02–1.02], MW p=6.1e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pairs** (64B, N=4): ratio sysv/posix = 1.02× [1.01–1.02], MW p=5.8e-07, Cliff δ=-0.93 (large), winner=mq_posix
- **MQ pairs** (64B, N=4): ratio sysv/posix = 1.01× [1.01–1.02], MW p=5.3e-06, Cliff δ=-0.84 (large), winner=mq_posix
- **MQ pairs** (64B, N=4): ratio sysv/posix = 0.52× [0.47–0.61], MW p=1.3e-05, Cliff δ=0.81 (large), winner=mq_sysv
- **MQ pairs** (64B, N=8): ratio sysv/posix = 1.01× [1.01–1.01], MW p=8.9e-07, Cliff δ=-0.91 (large), winner=mq_posix
- **MQ pairs** (64B, N=8): ratio sysv/posix = 1.01× [1.00–1.02], MW p=8.7e-04, Cliff δ=-0.62 (large), winner=mq_posix
- **MQ pairs** (64B, N=8): ratio sysv/posix = 0.28× [0.27–0.30], MW p=1.4e-07, Cliff δ=0.97 (large), winner=mq_sysv
- **MQ pairs** (256B, N=1): ratio sysv/posix = 1.02× [1.02–1.04], MW p=2.1e-03, Cliff δ=-0.57 (large), winner=mq_posix
- **MQ pairs** (256B, N=1): ratio sysv/posix = 1.02× [1.00–1.04], MW p=9.4e-03, Cliff δ=-0.48 (large), winner=mq_posix
- **MQ pairs** (256B, N=1): ratio sysv/posix = 0.78× [0.66–1.02], MW p=3.4e-02, Cliff δ=0.40 (medium), winner=mq_sysv
- **MQ pairs** (256B, N=2): ratio sysv/posix = 1.01× [1.01–1.04], MW p=7.3e-05, Cliff δ=-0.73 (large), winner=mq_posix
- **MQ pairs** (256B, N=2): ratio sysv/posix = 1.01× [1.00–1.04], MW p=5.8e-03, Cliff δ=-0.51 (large), winner=mq_posix
- **MQ pairs** (256B, N=2): ratio sysv/posix = 0.23× [0.21–0.30], MW p=2.2e-07, Cliff δ=0.96 (large), winner=mq_sysv
- **MQ pairs** (256B, N=4): ratio sysv/posix = 1.02× [1.01–1.02], MW p=2.3e-03, Cliff δ=-0.56 (large), winner=mq_posix
- **MQ pairs** (256B, N=4): ratio sysv/posix = 1.00× [0.99–1.01], MW p=4.2e-02, Cliff δ=-0.38 (medium), winner=mq_posix
- **MQ pairs** (256B, N=4): ratio sysv/posix = 0.55× [0.48–0.63], MW p=7.6e-06, Cliff δ=0.83 (large), winner=mq_sysv
- **MQ pairs** (256B, N=8): ratio sysv/posix = 1.00× [1.00–1.01], MW p=3.3e-03, Cliff δ=-0.55 (large), winner=mq_posix
- **MQ pairs** (256B, N=8): ratio sysv/posix = 0.97× [0.96–1.00], MW p=2.0e-03, Cliff δ=0.57 (large), winner=mq_sysv
- **MQ pairs** (256B, N=8): ratio sysv/posix = 1.01× [1.00–1.02], MW p=1.4e-02, Cliff δ=-0.46 (medium), winner=mq_posix
- **MQ pairs** (256B, N=8): ratio sysv/posix = 0.27× [0.26–0.28], MW p=1.2e-06, Cliff δ=0.90 (large), winner=mq_sysv
- **MQ pairs** (1024B, N=1): ratio sysv/posix = 1.01× [1.00–1.05], MW p=2.1e-02, Cliff δ=-0.43 (medium), winner=mq_posix
- **MQ pairs** (1024B, N=1): ratio sysv/posix = 1.02× [1.01–1.04], MW p=3.7e-04, Cliff δ=-0.66 (large), winner=mq_posix
- **MQ pairs** (1024B, N=1): ratio sysv/posix = 0.68× [0.57–0.87], MW p=2.0e-03, Cliff δ=0.57 (large), winner=mq_sysv
- **MQ pairs** (1024B, N=2): ratio sysv/posix = 0.15× [0.12–0.22], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_sysv
- **MQ pairs** (1024B, N=4): ratio sysv/posix = 1.02× [1.00–1.03], MW p=3.9e-04, Cliff δ=-0.66 (large), winner=mq_posix
- **MQ pairs** (1024B, N=4): ratio sysv/posix = 1.01× [0.99–1.03], MW p=1.4e-02, Cliff δ=-0.46 (medium), winner=mq_posix
- **MQ pairs** (1024B, N=4): ratio sysv/posix = 1.01× [0.99–1.03], MW p=4.1e-02, Cliff δ=-0.38 (medium), winner=mq_posix
- **MQ pairs** (1024B, N=4): ratio sysv/posix = 0.55× [0.44–0.61], MW p=6.7e-06, Cliff δ=0.83 (large), winner=mq_sysv
- **MQ pairs** (1024B, N=8): ratio sysv/posix = 1.01× [1.00–1.02], MW p=1.4e-02, Cliff δ=-0.46 (medium), winner=mq_posix
- **MQ pairs** (1024B, N=8): ratio sysv/posix = 0.97× [0.95–0.99], MW p=9.7e-04, Cliff δ=0.61 (large), winner=mq_sysv
- **MQ pairs** (1024B, N=8): ratio sysv/posix = 0.29× [0.28–0.30], MW p=3.5e-06, Cliff δ=0.86 (large), winner=mq_sysv
- **MQ pingpong** (diff_l3, 8B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.7e-06, Cliff δ=-0.83 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 8B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=1.5e-05, Cliff δ=-0.80 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 8B): ratio sysv/posix = 0.08× [0.07–0.08], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_sysv
- **MQ pingpong** (same_core, 8B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.1e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pingpong** (same_core, 8B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=5.8e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 8B): ratio sysv/posix = 1.02× [1.02–1.03], MW p=8.1e-07, Cliff δ=-0.91 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 8B): ratio sysv/posix = 1.03× [1.02–1.03], MW p=2.2e-06, Cliff δ=-0.87 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 8B): ratio sysv/posix = 1.03× [1.02–1.07], MW p=1.7e-02, Cliff δ=-0.45 (medium), winner=mq_posix
- **MQ pingpong** (same_l3, 8B): ratio sysv/posix = 1.02× [1.00–1.07], MW p=1.9e-02, Cliff δ=-0.43 (medium), winner=mq_posix
- **MQ pingpong** (diff_l3, 64B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=1.0e-06, Cliff δ=-0.91 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 64B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=1.6e-06, Cliff δ=-0.89 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 64B): ratio sysv/posix = 0.99× [0.98–1.00], MW p=3.1e-02, Cliff δ=0.40 (medium), winner=mq_sysv
- **MQ pingpong** (diff_l3, 64B): ratio sysv/posix = 0.08× [0.07–0.08], MW p=6.6e-05, Cliff δ=0.74 (large), winner=mq_sysv
- **MQ pingpong** (same_core, 64B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.4e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pingpong** (same_core, 64B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.4e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 64B): ratio sysv/posix = 1.02× [1.02–1.03], MW p=6.0e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 64B): ratio sysv/posix = 1.02× [1.02–1.03], MW p=9.9e-07, Cliff δ=-0.90 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 256B): ratio sysv/posix = 1.01× [0.98–1.01], MW p=4.6e-02, Cliff δ=-0.37 (medium), winner=mq_posix
- **MQ pingpong** (diff_l3, 256B): ratio sysv/posix = 0.98× [0.96–0.99], MW p=8.3e-05, Cliff δ=0.73 (large), winner=mq_sysv
- **MQ pingpong** (diff_l3, 256B): ratio sysv/posix = 0.08× [0.08–0.08], MW p=9.1e-07, Cliff δ=0.91 (large), winner=mq_sysv
- **MQ pingpong** (same_core, 256B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=1.5e-02, Cliff δ=-0.45 (medium), winner=mq_posix
- **MQ pingpong** (same_core, 256B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=1.0e-02, Cliff δ=-0.47 (large), winner=mq_posix
- **MQ pingpong** (same_core, 256B): ratio sysv/posix = 1.02× [1.00–1.05], MW p=2.5e-02, Cliff δ=-0.42 (medium), winner=mq_posix
- **MQ pingpong** (same_l3, 256B): ratio sysv/posix = 1.02× [1.00–1.04], MW p=4.4e-03, Cliff δ=-0.53 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 256B): ratio sysv/posix = 1.01× [1.00–1.04], MW p=5.6e-03, Cliff δ=-0.51 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 256B): ratio sysv/posix = 1.03× [1.00–1.05], MW p=5.8e-03, Cliff δ=-0.51 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 1024B): ratio sysv/posix = 0.08× [0.08–0.08], MW p=1.4e-05, Cliff δ=0.81 (large), winner=mq_sysv
- **MQ pingpong** (same_core, 1024B): ratio sysv/posix = 1.01× [1.01–1.03], MW p=3.3e-04, Cliff δ=-0.67 (large), winner=mq_posix
- **MQ pingpong** (same_core, 1024B): ratio sysv/posix = 1.01× [1.01–1.02], MW p=2.3e-03, Cliff δ=-0.56 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 1024B): ratio sysv/posix = 1.02× [1.00–1.05], MW p=5.1e-03, Cliff δ=-0.52 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 1024B): ratio sysv/posix = 1.02× [1.02–1.03], MW p=5.3e-04, Cliff δ=-0.64 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 1024B): ratio sysv/posix = 1.03× [1.00–1.06], MW p=2.4e-02, Cliff δ=-0.42 (medium), winner=mq_posix
- **MQ pingpong** (diff_l3, 4096B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=7.2e-03, Cliff δ=-0.50 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 4096B): ratio sysv/posix = 0.99× [0.98–0.99], MW p=7.3e-05, Cliff δ=0.73 (large), winner=mq_sysv
- **MQ pingpong** (diff_l3, 4096B): ratio sysv/posix = 0.10× [0.10–0.10], MW p=9.7e-06, Cliff δ=0.82 (large), winner=mq_sysv
- **MQ pingpong** (same_core, 4096B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=1.2e-06, Cliff δ=-0.90 (large), winner=mq_posix
- **MQ pingpong** (same_core, 4096B): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.3e-06, Cliff δ=-0.83 (large), winner=mq_posix
- **MQ pingpong** (same_core, 4096B): ratio sysv/posix = 1.01× [1.00–1.02], MW p=5.0e-02, Cliff δ=-0.36 (medium), winner=mq_posix
- **MQ pingpong** (same_l3, 4096B): ratio sysv/posix = 1.02× [1.02–1.02], MW p=6.5e-08, Cliff δ=-1.00 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 4096B): ratio sysv/posix = 1.02× [1.02–1.02], MW p=8.9e-08, Cliff δ=-0.99 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 4096B): ratio sysv/posix = 1.01× [1.01–1.02], MW p=5.1e-03, Cliff δ=-0.52 (large), winner=mq_posix
- **MQ pingpong** (diff_l3, 8192B): ratio sysv/posix = 1.00× [1.00–1.00], MW p=3.8e-02, Cliff δ=0.39 (medium), winner=mq_sysv
- **MQ pingpong** (diff_l3, 8192B): ratio sysv/posix = 1.00× [0.99–1.00], MW p=1.7e-05, Cliff δ=0.80 (large), winner=mq_sysv
- **MQ pingpong** (diff_l3, 8192B): ratio sysv/posix = 0.99× [0.99–0.99], MW p=1.4e-07, Cliff δ=0.97 (large), winner=mq_sysv
- **MQ pingpong** (diff_l3, 8192B): ratio sysv/posix = 0.12× [0.12–0.12], MW p=2.1e-06, Cliff δ=0.88 (large), winner=mq_sysv
- **MQ pingpong** (same_core, 8192B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=3.1e-07, Cliff δ=-0.95 (large), winner=mq_posix
- **MQ pingpong** (same_core, 8192B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=3.2e-04, Cliff δ=-0.67 (large), winner=mq_posix
- **MQ pingpong** (same_core, 8192B): ratio sysv/posix = 1.06× [1.03–1.08], MW p=6.2e-05, Cliff δ=-0.74 (large), winner=mq_posix
- **MQ pingpong** (same_core, 8192B): ratio sysv/posix = 1.01× [1.00–1.02], MW p=2.1e-02, Cliff δ=-0.43 (medium), winner=mq_posix
- **MQ pingpong** (same_l3, 8192B): ratio sysv/posix = 1.02× [1.01–1.02], MW p=1.1e-06, Cliff δ=-0.90 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 8192B): ratio sysv/posix = 1.02× [1.01–1.02], MW p=1.3e-06, Cliff δ=-0.90 (large), winner=mq_posix
- **MQ pingpong** (same_l3, 8192B): ratio sysv/posix = 1.02× [1.01–1.03], MW p=1.4e-03, Cliff δ=-0.59 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 8B, q=1): ratio sysv/posix = 1.03× [1.02–1.04], MW p=2.3e-06, Cliff δ=-0.88 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8B, q=1): ratio sysv/posix = 1.00× [1.00–1.01], MW p=2.7e-07, Cliff δ=-0.95 (large), winner=mq_sysv
- **MQ throughput** (same_l3, 8B, q=1): ratio sysv/posix = 1.12× [1.10–1.13], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 8B, q=8): ratio sysv/posix = 1.03× [1.02–1.04], MW p=7.9e-07, Cliff δ=-0.92 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8B, q=8): ratio sysv/posix = 0.94× [0.93–0.94], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8B, q=8): ratio sysv/posix = 0.96× [0.96–0.97], MW p=1.1e-07, Cliff δ=0.98 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 8B, q=32): ratio sysv/posix = 1.05× [1.05–1.06], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8B, q=32): ratio sysv/posix = 0.94× [0.93–0.94], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8B, q=32): ratio sysv/posix = 0.95× [0.94–0.95], MW p=7.9e-08, Cliff δ=0.99 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 8B, q=128): ratio sysv/posix = 1.01× [1.00–1.02], MW p=3.5e-02, Cliff δ=-0.39 (medium), winner=mq_sysv
- **MQ throughput** (same_core, 8B, q=128): ratio sysv/posix = 0.94× [0.94–0.95], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8B, q=128): ratio sysv/posix = 0.94× [0.94–0.95], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 8B, q=512): ratio sysv/posix = 1.05× [1.04–1.05], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8B, q=512): ratio sysv/posix = 0.95× [0.94–0.95], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8B, q=512): ratio sysv/posix = 0.95× [0.95–0.96], MW p=7.8e-08, Cliff δ=0.99 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 64B, q=1): ratio sysv/posix = 0.76× [0.75–0.76], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_core, 64B, q=1): ratio sysv/posix = 0.75× [0.75–0.75], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 64B, q=1): ratio sysv/posix = 0.71× [0.70–0.71], MW p=1.2e-06, Cliff δ=0.90 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 64B, q=8): ratio sysv/posix = 0.95× [0.94–0.96], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_core, 64B, q=8): ratio sysv/posix = 0.95× [0.94–0.95], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 64B, q=8): ratio sysv/posix = 0.93× [0.93–0.94], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 64B, q=32): ratio sysv/posix = 1.13× [1.12–1.13], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 64B, q=32): ratio sysv/posix = 0.93× [0.93–0.94], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 64B, q=32): ratio sysv/posix = 0.96× [0.95–0.97], MW p=9.2e-08, Cliff δ=0.99 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 64B, q=128): ratio sysv/posix = 1.06× [1.05–1.06], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 64B, q=128): ratio sysv/posix = 0.95× [0.94–0.95], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 64B, q=128): ratio sysv/posix = 0.96× [0.95–0.97], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 64B, q=512): ratio sysv/posix = 1.05× [1.04–1.05], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 64B, q=512): ratio sysv/posix = 0.95× [0.94–0.95], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 64B, q=512): ratio sysv/posix = 0.96× [0.96–0.97], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 256B, q=1): ratio sysv/posix = 0.77× [0.76–0.77], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_core, 256B, q=1): ratio sysv/posix = 0.75× [0.75–0.75], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 256B, q=1): ratio sysv/posix = 0.71× [0.70–0.72], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 256B, q=8): ratio sysv/posix = 1.02× [1.02–1.04], MW p=7.9e-07, Cliff δ=-0.92 (large), winner=mq_sysv
- **MQ throughput** (same_core, 256B, q=8): ratio sysv/posix = 0.93× [0.92–0.93], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 256B, q=8): ratio sysv/posix = 1.04× [1.03–1.06], MW p=4.5e-07, Cliff δ=-0.94 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 256B, q=32): ratio sysv/posix = 1.04× [1.04–1.05], MW p=1.7e-07, Cliff δ=-0.97 (large), winner=mq_sysv
- **MQ throughput** (same_core, 256B, q=32): ratio sysv/posix = 0.93× [0.92–0.93], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 256B, q=32): ratio sysv/posix = 1.06× [1.04–1.07], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 256B, q=128): ratio sysv/posix = 1.03× [1.02–1.03], MW p=1.1e-07, Cliff δ=-0.98 (large), winner=mq_sysv
- **MQ throughput** (same_core, 256B, q=128): ratio sysv/posix = 0.93× [0.92–0.93], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 256B, q=128): ratio sysv/posix = 1.05× [1.03–1.07], MW p=4.5e-07, Cliff δ=-0.94 (large), winner=mq_sysv
- **MQ throughput** (same_core, 256B, q=512): ratio sysv/posix = 0.93× [0.92–0.93], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 256B, q=512): ratio sysv/posix = 1.05× [1.03–1.07], MW p=6.0e-07, Cliff δ=-0.93 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 1024B, q=1): ratio sysv/posix = 0.77× [0.77–0.77], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_core, 1024B, q=1): ratio sysv/posix = 0.74× [0.74–0.75], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 1024B, q=1): ratio sysv/posix = 0.74× [0.74–0.75], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 1024B, q=8): ratio sysv/posix = 1.12× [1.12–1.13], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 1024B, q=8): ratio sysv/posix = 0.90× [0.90–0.90], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 1024B, q=8): ratio sysv/posix = 1.02× [1.01–1.03], MW p=1.3e-05, Cliff δ=-0.81 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 1024B, q=32): ratio sysv/posix = 1.12× [1.11–1.13], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 1024B, q=32): ratio sysv/posix = 0.92× [0.92–0.92], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 1024B, q=32): ratio sysv/posix = 1.02× [1.01–1.03], MW p=2.5e-04, Cliff δ=-0.68 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 1024B, q=128): ratio sysv/posix = 1.09× [1.08–1.10], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 1024B, q=128): ratio sysv/posix = 0.95× [0.95–0.96], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 1024B, q=128): ratio sysv/posix = 1.01× [1.00–1.02], MW p=2.7e-02, Cliff δ=-0.41 (medium), winner=mq_sysv
- **MQ throughput** (diff_l3, 1024B, q=512): ratio sysv/posix = 1.07× [1.06–1.08], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 1024B, q=512): ratio sysv/posix = 0.95× [0.95–0.96], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 1024B, q=512): ratio sysv/posix = 1.01× [1.00–1.02], MW p=3.4e-02, Cliff δ=-0.40 (medium), winner=mq_sysv
- **MQ throughput** (diff_l3, 4096B, q=1): ratio sysv/posix = 0.82× [0.81–0.82], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_core, 4096B, q=1): ratio sysv/posix = 0.76× [0.76–0.76], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 4096B, q=1): ratio sysv/posix = 0.82× [0.82–0.82], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 4096B, q=8): ratio sysv/posix = 1.09× [1.08–1.09], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 4096B, q=8): ratio sysv/posix = 0.95× [0.95–0.96], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 4096B, q=8): ratio sysv/posix = 1.03× [1.02–1.03], MW p=1.1e-07, Cliff δ=-0.98 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 4096B, q=32): ratio sysv/posix = 1.07× [1.06–1.07], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 4096B, q=32): ratio sysv/posix = 0.96× [0.96–0.97], MW p=7.9e-08, Cliff δ=0.99 (large), winner=mq_posix
- **MQ throughput** (same_l3, 4096B, q=32): ratio sysv/posix = 1.03× [1.03–1.04], MW p=9.2e-08, Cliff δ=-0.99 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 4096B, q=128): ratio sysv/posix = 1.02× [1.02–1.02], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 4096B, q=128): ratio sysv/posix = 0.98× [0.98–0.98], MW p=1.9e-07, Cliff δ=0.96 (large), winner=mq_posix
- **MQ throughput** (same_l3, 4096B, q=128): ratio sysv/posix = 1.03× [1.02–1.03], MW p=7.9e-08, Cliff δ=-0.99 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 4096B, q=512): ratio sysv/posix = 1.03× [1.02–1.04], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 4096B, q=512): ratio sysv/posix = 0.98× [0.98–0.98], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 4096B, q=512): ratio sysv/posix = 1.03× [1.02–1.04], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8192B, q=1): ratio sysv/posix = 0.79× [0.79–0.80], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8192B, q=1): ratio sysv/posix = 0.81× [0.81–0.82], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (diff_l3, 8192B, q=8): ratio sysv/posix = 1.05× [1.05–1.05], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8192B, q=8): ratio sysv/posix = 0.98× [0.98–0.99], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8192B, q=8): ratio sysv/posix = 1.03× [1.02–1.03], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 8192B, q=32): ratio sysv/posix = 1.03× [1.02–1.03], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (same_core, 8192B, q=32): ratio sysv/posix = 0.98× [0.98–0.98], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8192B, q=32): ratio sysv/posix = 1.02× [1.02–1.03], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 8192B, q=128): ratio sysv/posix = 0.98× [0.97–0.98], MW p=1.2e-03, Cliff δ=0.60 (large), winner=mq_posix
- **MQ throughput** (same_core, 8192B, q=128): ratio sysv/posix = 0.98× [0.98–0.98], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8192B, q=128): ratio sysv/posix = 1.02× [1.02–1.03], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **MQ throughput** (diff_l3, 8192B, q=512): ratio sysv/posix = 0.97× [0.97–0.98], MW p=1.6e-05, Cliff δ=0.80 (large), winner=mq_posix
- **MQ throughput** (same_core, 8192B, q=512): ratio sysv/posix = 0.98× [0.98–0.99], MW p=6.8e-08, Cliff δ=1.00 (large), winner=mq_posix
- **MQ throughput** (same_l3, 8192B, q=512): ratio sysv/posix = 1.03× [1.02–1.03], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=mq_sysv
- **SHM pairs** (64B, N=1): ratio sysv/posix = 1.00× [1.00–1.01], MW p=1.2e-07, Cliff δ=-0.98 (large), winner=shm_posix
- **SHM pairs** (64B, N=2): ratio sysv/posix = 0.97× [0.97–0.97], MW p=5.5e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (64B, N=2): ratio sysv/posix = 0.97× [0.96–0.97], MW p=1.2e-06, Cliff δ=0.90 (large), winner=shm_sysv
- **SHM pairs** (64B, N=2): ratio sysv/posix = 0.98× [0.96–1.00], MW p=2.8e-03, Cliff δ=0.56 (large), winner=shm_sysv
- **SHM pairs** (64B, N=2): ratio sysv/posix = 0.30× [0.26–0.39], MW p=4.5e-06, Cliff δ=0.85 (large), winner=shm_sysv
- **SHM pairs** (64B, N=4): ratio sysv/posix = 0.99× [0.99–0.99], MW p=3.7e-07, Cliff δ=0.94 (large), winner=shm_sysv
- **SHM pairs** (64B, N=4): ratio sysv/posix = 0.99× [0.99–0.99], MW p=1.1e-05, Cliff δ=0.81 (large), winner=shm_sysv
- **SHM pairs** (64B, N=4): ratio sysv/posix = 0.91× [0.81–0.97], MW p=3.0e-04, Cliff δ=0.67 (large), winner=shm_sysv
- **SHM pairs** (64B, N=8): ratio sysv/posix = 1.02× [1.01–1.02], MW p=6.7e-08, Cliff δ=-1.00 (large), winner=shm_posix
- **SHM pairs** (64B, N=8): ratio sysv/posix = 0.94× [0.93–0.94], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (64B, N=8): ratio sysv/posix = 0.90× [0.90–0.91], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (64B, N=8): ratio sysv/posix = 0.23× [0.23–0.24], MW p=3.9e-07, Cliff δ=0.94 (large), winner=shm_sysv
- **SHM pairs** (256B, N=1): ratio sysv/posix = 1.00× [1.00–1.00], MW p=1.4e-04, Cliff δ=-0.70 (large), winner=shm_posix
- **SHM pairs** (256B, N=2): ratio sysv/posix = 0.98× [0.98–0.98], MW p=1.1e-06, Cliff δ=0.90 (large), winner=shm_sysv
- **SHM pairs** (256B, N=2): ratio sysv/posix = 0.97× [0.96–0.98], MW p=1.0e-06, Cliff δ=0.91 (large), winner=shm_sysv
- **SHM pairs** (256B, N=2): ratio sysv/posix = 0.97× [0.96–0.99], MW p=3.2e-03, Cliff δ=0.55 (large), winner=shm_sysv
- **SHM pairs** (256B, N=2): ratio sysv/posix = 0.37× [0.25–0.42], MW p=1.0e-06, Cliff δ=0.91 (large), winner=shm_sysv
- **SHM pairs** (256B, N=4): ratio sysv/posix = 0.99× [0.99–0.99], MW p=9.8e-07, Cliff δ=0.91 (large), winner=shm_sysv
- **SHM pairs** (256B, N=4): ratio sysv/posix = 0.99× [0.99–0.99], MW p=1.1e-05, Cliff δ=0.81 (large), winner=shm_sysv
- **SHM pairs** (256B, N=4): ratio sysv/posix = 0.92× [0.84–0.96], MW p=4.6e-04, Cliff δ=0.65 (large), winner=shm_sysv
- **SHM pairs** (256B, N=8): ratio sysv/posix = 1.02× [1.02–1.03], MW p=2.5e-07, Cliff δ=-0.95 (large), winner=shm_posix
- **SHM pairs** (256B, N=8): ratio sysv/posix = 0.94× [0.93–0.94], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (256B, N=8): ratio sysv/posix = 0.90× [0.90–0.91], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (256B, N=8): ratio sysv/posix = 0.23× [0.23–0.23], MW p=2.2e-07, Cliff δ=0.96 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=1): ratio sysv/posix = 1.00× [1.00–1.01], MW p=5.9e-07, Cliff δ=-0.92 (large), winner=shm_posix
- **SHM pairs** (1024B, N=2): ratio sysv/posix = 0.97× [0.96–0.97], MW p=6.6e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=2): ratio sysv/posix = 0.96× [0.96–0.97], MW p=6.6e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=2): ratio sysv/posix = 0.97× [0.96–0.99], MW p=6.5e-04, Cliff δ=0.63 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=2): ratio sysv/posix = 0.23× [0.21–0.25], MW p=1.1e-07, Cliff δ=0.98 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=4): ratio sysv/posix = 1.00× [1.00–1.00], MW p=1.9e-06, Cliff δ=-0.87 (large), winner=shm_posix
- **SHM pairs** (1024B, N=4): ratio sysv/posix = 0.99× [0.99–0.99], MW p=6.2e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=4): ratio sysv/posix = 0.99× [0.99–0.99], MW p=6.9e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=4): ratio sysv/posix = 0.94× [0.87–0.98], MW p=6.9e-04, Cliff δ=0.63 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=8): ratio sysv/posix = 1.01× [1.00–1.01], MW p=4.0e-05, Cliff δ=-0.76 (large), winner=shm_posix
- **SHM pairs** (1024B, N=8): ratio sysv/posix = 0.93× [0.92–0.93], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=8): ratio sysv/posix = 0.91× [0.91–0.92], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pairs** (1024B, N=8): ratio sysv/posix = 0.24× [0.23–0.24], MW p=6.9e-07, Cliff δ=0.92 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8B): ratio sysv/posix = 0.98× [0.98–0.98], MW p=6.0e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8B): ratio sysv/posix = 0.98× [0.97–0.98], MW p=6.4e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8B): ratio sysv/posix = 0.95× [0.95–0.95], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8B): ratio sysv/posix = 0.07× [0.07–0.07], MW p=9.0e-08, Cliff δ=0.99 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 8B): ratio sysv/posix = 0.98× [0.98–0.99], MW p=6.5e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 8B): ratio sysv/posix = 1.09× [1.02–1.10], MW p=1.3e-06, Cliff δ=-0.90 (large), winner=shm_posix
- **SHM pingpong** (same_core, 8B): ratio sysv/posix = 0.98× [0.98–0.99], MW p=7.9e-04, Cliff δ=0.62 (large), winner=shm_sysv
- **SHM pingpong** (same_l3, 8B): ratio sysv/posix = 1.00× [1.00–1.01], MW p=9.0e-08, Cliff δ=-0.98 (large), winner=shm_posix
- **SHM pingpong** (diff_l3, 64B): ratio sysv/posix = 0.98× [0.97–0.98], MW p=1.8e-07, Cliff δ=0.96 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 64B): ratio sysv/posix = 0.97× [0.97–0.98], MW p=3.9e-07, Cliff δ=0.94 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 64B): ratio sysv/posix = 0.95× [0.94–0.96], MW p=9.8e-08, Cliff δ=0.99 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 64B): ratio sysv/posix = 0.07× [0.07–0.07], MW p=3.1e-06, Cliff δ=0.86 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 64B): ratio sysv/posix = 0.98× [0.98–0.98], MW p=6.3e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 64B): ratio sysv/posix = 1.10× [1.03–1.11], MW p=1.6e-07, Cliff δ=-0.97 (large), winner=shm_posix
- **SHM pingpong** (same_core, 64B): ratio sysv/posix = 0.97× [0.97–0.99], MW p=1.4e-03, Cliff δ=0.59 (large), winner=shm_sysv
- **SHM pingpong** (same_l3, 64B): ratio sysv/posix = 1.00× [1.00–1.01], MW p=1.8e-07, Cliff δ=-0.96 (large), winner=shm_posix
- **SHM pingpong** (same_l3, 64B): ratio sysv/posix = 0.96× [0.94–1.00], MW p=4.4e-02, Cliff δ=0.38 (medium), winner=shm_sysv
- **SHM pingpong** (same_l3, 64B): ratio sysv/posix = 0.96× [0.91–0.99], MW p=3.6e-03, Cliff δ=0.54 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 256B): ratio sysv/posix = 0.98× [0.98–0.99], MW p=1.5e-07, Cliff δ=0.97 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 256B): ratio sysv/posix = 0.98× [0.97–0.99], MW p=7.8e-08, Cliff δ=0.99 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 256B): ratio sysv/posix = 0.95× [0.95–0.96], MW p=1.1e-07, Cliff δ=0.98 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 256B): ratio sysv/posix = 0.07× [0.07–0.07], MW p=3.9e-07, Cliff δ=0.94 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 256B): ratio sysv/posix = 0.98× [0.98–0.98], MW p=6.3e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 256B): ratio sysv/posix = 1.11× [1.02–1.11], MW p=2.4e-06, Cliff δ=-0.87 (large), winner=shm_posix
- **SHM pingpong** (same_core, 256B): ratio sysv/posix = 0.99× [0.98–1.00], MW p=4.0e-03, Cliff δ=0.54 (large), winner=shm_sysv
- **SHM pingpong** (same_l3, 256B): ratio sysv/posix = 1.01× [1.00–1.01], MW p=2.0e-07, Cliff δ=-0.96 (large), winner=shm_posix
- **SHM pingpong** (diff_l3, 1024B): ratio sysv/posix = 0.97× [0.97–0.98], MW p=6.6e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 1024B): ratio sysv/posix = 0.97× [0.96–0.98], MW p=6.5e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 1024B): ratio sysv/posix = 0.94× [0.94–0.96], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 1024B): ratio sysv/posix = 0.07× [0.07–0.07], MW p=1.7e-07, Cliff δ=0.97 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 1024B): ratio sysv/posix = 0.98× [0.98–0.99], MW p=6.6e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 1024B): ratio sysv/posix = 1.10× [1.10–1.11], MW p=1.7e-07, Cliff δ=-0.97 (large), winner=shm_posix
- **SHM pingpong** (same_core, 1024B): ratio sysv/posix = 0.98× [0.97–0.99], MW p=1.2e-04, Cliff δ=0.71 (large), winner=shm_sysv
- **SHM pingpong** (same_l3, 1024B): ratio sysv/posix = 1.00× [1.00–1.01], MW p=3.0e-07, Cliff δ=-0.94 (large), winner=shm_posix
- **SHM pingpong** (same_l3, 1024B): ratio sysv/posix = 1.00× [1.00–1.00], MW p=1.2e-02, Cliff δ=0.46 (medium), winner=shm_sysv
- **SHM pingpong** (diff_l3, 4096B): ratio sysv/posix = 0.97× [0.97–0.98], MW p=6.6e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 4096B): ratio sysv/posix = 0.97× [0.97–0.97], MW p=6.5e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 4096B): ratio sysv/posix = 0.94× [0.94–0.95], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 4096B): ratio sysv/posix = 0.07× [0.07–0.07], MW p=1.1e-07, Cliff δ=0.98 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 4096B): ratio sysv/posix = 0.99× [0.99–0.99], MW p=6.3e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 4096B): ratio sysv/posix = 1.11× [1.11–1.12], MW p=6.3e-08, Cliff δ=-1.00 (large), winner=shm_posix
- **SHM pingpong** (same_core, 4096B): ratio sysv/posix = 1.01× [1.00–1.02], MW p=1.5e-02, Cliff δ=-0.45 (medium), winner=shm_posix
- **SHM pingpong** (same_l3, 4096B): ratio sysv/posix = 1.00× [1.00–1.01], MW p=1.6e-07, Cliff δ=-0.96 (large), winner=shm_posix
- **SHM pingpong** (same_l3, 4096B): ratio sysv/posix = 1.00× [1.00–1.00], MW p=5.3e-04, Cliff δ=0.64 (large), winner=shm_sysv
- **SHM pingpong** (same_l3, 4096B): ratio sysv/posix = 0.97× [0.94–1.00], MW p=1.7e-02, Cliff δ=0.45 (medium), winner=shm_sysv
- **SHM pingpong** (same_l3, 4096B): ratio sysv/posix = 0.97× [0.92–1.00], MW p=5.1e-03, Cliff δ=0.52 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8192B): ratio sysv/posix = 0.97× [0.97–0.97], MW p=6.6e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8192B): ratio sysv/posix = 0.97× [0.96–0.97], MW p=1.7e-07, Cliff δ=0.97 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8192B): ratio sysv/posix = 0.94× [0.93–0.95], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_sysv
- **SHM pingpong** (diff_l3, 8192B): ratio sysv/posix = 0.07× [0.07–0.07], MW p=2.2e-07, Cliff δ=0.96 (large), winner=shm_sysv
- **SHM pingpong** (same_core, 8192B): ratio sysv/posix = 1.12× [1.11–1.12], MW p=1.2e-03, Cliff δ=-0.60 (large), winner=shm_posix
- **SHM pingpong** (same_core, 8192B): ratio sysv/posix = 1.12× [1.12–1.12], MW p=6.0e-08, Cliff δ=-1.00 (large), winner=shm_posix
- **SHM pingpong** (same_core, 8192B): ratio sysv/posix = 1.06× [1.04–1.07], MW p=4.5e-07, Cliff δ=-0.94 (large), winner=shm_posix
- **SHM pingpong** (same_core, 8192B): ratio sysv/posix = 1.03× [1.02–1.04], MW p=5.5e-06, Cliff δ=-0.84 (large), winner=shm_posix
- **SHM pingpong** (same_l3, 8192B): ratio sysv/posix = 1.00× [1.00–1.00], MW p=7.9e-06, Cliff δ=-0.82 (large), winner=shm_posix
- **SHM pingpong** (same_l3, 8192B): ratio sysv/posix = 1.00× [0.99–1.00], MW p=5.0e-07, Cliff δ=0.92 (large), winner=shm_sysv
- **SHM throughput** (diff_l3, 8B, q=1): ratio sysv/posix = 1.03× [1.03–1.04], MW p=5.4e-05, Cliff δ=-0.75 (large), winner=shm_sysv
- **SHM throughput** (same_core, 8B, q=1): ratio sysv/posix = 1.01× [1.01–1.01], MW p=5.3e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_l3, 8B, q=1): ratio sysv/posix = 0.99× [0.97–0.99], MW p=6.3e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8B, q=8): ratio sysv/posix = 0.33× [0.30–0.36], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8B, q=8): ratio sysv/posix = 0.56× [0.56–0.57], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8B, q=8): ratio sysv/posix = 0.14× [0.14–0.15], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8B, q=32): ratio sysv/posix = 0.25× [0.24–0.28], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8B, q=32): ratio sysv/posix = 0.54× [0.53–0.54], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8B, q=32): ratio sysv/posix = 0.18× [0.16–0.22], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8B, q=128): ratio sysv/posix = 0.20× [0.18–0.22], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8B, q=128): ratio sysv/posix = 0.53× [0.52–0.53], MW p=6.7e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8B, q=128): ratio sysv/posix = 0.05× [0.05–0.06], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8B, q=512): ratio sysv/posix = 0.18× [0.17–0.19], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8B, q=512): ratio sysv/posix = 0.52× [0.51–0.52], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8B, q=512): ratio sysv/posix = 0.04× [0.04–0.05], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 64B, q=1): ratio sysv/posix = 1.03× [1.03–1.04], MW p=1.2e-05, Cliff δ=-0.81 (large), winner=shm_sysv
- **SHM throughput** (same_core, 64B, q=1): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.7e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_l3, 64B, q=1): ratio sysv/posix = 0.99× [0.99–1.00], MW p=5.1e-06, Cliff δ=0.84 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 64B, q=8): ratio sysv/posix = 0.24× [0.22–0.27], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 64B, q=8): ratio sysv/posix = 0.56× [0.56–0.56], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 64B, q=8): ratio sysv/posix = 0.40× [0.37–0.42], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 64B, q=32): ratio sysv/posix = 0.24× [0.20–0.26], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 64B, q=32): ratio sysv/posix = 0.54× [0.54–0.55], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 64B, q=32): ratio sysv/posix = 0.30× [0.28–0.32], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 64B, q=128): ratio sysv/posix = 0.22× [0.21–0.24], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 64B, q=128): ratio sysv/posix = 0.54× [0.53–0.54], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 64B, q=128): ratio sysv/posix = 0.17× [0.15–0.21], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 64B, q=512): ratio sysv/posix = 0.20× [0.19–0.21], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 64B, q=512): ratio sysv/posix = 0.53× [0.52–0.53], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 64B, q=512): ratio sysv/posix = 0.09× [0.08–0.11], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 256B, q=1): ratio sysv/posix = 1.04× [1.03–1.05], MW p=1.4e-06, Cliff δ=-0.90 (large), winner=shm_sysv
- **SHM throughput** (same_core, 256B, q=1): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_l3, 256B, q=1): ratio sysv/posix = 0.99× [0.98–0.99], MW p=4.5e-06, Cliff δ=0.85 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 256B, q=8): ratio sysv/posix = 0.38× [0.36–0.41], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 256B, q=8): ratio sysv/posix = 0.56× [0.56–0.57], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 256B, q=8): ratio sysv/posix = 0.11× [0.11–0.13], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 256B, q=32): ratio sysv/posix = 0.34× [0.31–0.35], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 256B, q=32): ratio sysv/posix = 0.53× [0.53–0.54], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 256B, q=32): ratio sysv/posix = 0.11× [0.10–0.13], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 256B, q=128): ratio sysv/posix = 0.36× [0.33–0.37], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 256B, q=128): ratio sysv/posix = 0.52× [0.52–0.53], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 256B, q=128): ratio sysv/posix = 0.09× [0.09–0.10], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 256B, q=512): ratio sysv/posix = 0.27× [0.24–0.28], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 256B, q=512): ratio sysv/posix = 0.49× [0.47–0.50], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 256B, q=512): ratio sysv/posix = 0.07× [0.07–0.08], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 1024B, q=1): ratio sysv/posix = 1.04× [1.03–1.05], MW p=4.0e-06, Cliff δ=-0.85 (large), winner=shm_sysv
- **SHM throughput** (same_core, 1024B, q=1): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_l3, 1024B, q=1): ratio sysv/posix = 0.99× [0.99–1.00], MW p=2.0e-05, Cliff δ=0.79 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 1024B, q=8): ratio sysv/posix = 0.44× [0.44–0.45], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 1024B, q=8): ratio sysv/posix = 0.55× [0.54–0.55], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 1024B, q=8): ratio sysv/posix = 0.26× [0.25–0.26], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 1024B, q=32): ratio sysv/posix = 0.41× [0.39–0.42], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 1024B, q=32): ratio sysv/posix = 0.52× [0.51–0.52], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 1024B, q=32): ratio sysv/posix = 0.21× [0.19–0.22], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 1024B, q=128): ratio sysv/posix = 0.32× [0.31–0.33], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 1024B, q=128): ratio sysv/posix = 0.38× [0.35–0.39], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 1024B, q=128): ratio sysv/posix = 0.21× [0.20–0.21], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 1024B, q=512): ratio sysv/posix = 0.23× [0.22–0.25], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 1024B, q=512): ratio sysv/posix = 0.11× [0.08–0.14], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 1024B, q=512): ratio sysv/posix = 0.18× [0.17–0.18], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 4096B, q=1): ratio sysv/posix = 1.06× [1.05–1.07], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_core, 4096B, q=1): ratio sysv/posix = 1.01× [1.01–1.01], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_l3, 4096B, q=1): ratio sysv/posix = 1.00× [0.99–1.00], MW p=7.4e-05, Cliff δ=0.73 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 4096B, q=8): ratio sysv/posix = 0.63× [0.62–0.67], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 4096B, q=8): ratio sysv/posix = 0.17× [0.16–0.17], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 4096B, q=8): ratio sysv/posix = 0.48× [0.47–0.49], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 4096B, q=32): ratio sysv/posix = 0.64× [0.63–0.65], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 4096B, q=32): ratio sysv/posix = 0.18× [0.18–0.19], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 4096B, q=32): ratio sysv/posix = 0.48× [0.48–0.48], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 4096B, q=128): ratio sysv/posix = 0.61× [0.61–0.62], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 4096B, q=128): ratio sysv/posix = 0.18× [0.17–0.18], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 4096B, q=128): ratio sysv/posix = 0.42× [0.42–0.43], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 4096B, q=512): ratio sysv/posix = 0.58× [0.58–0.59], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 4096B, q=512): ratio sysv/posix = 0.17× [0.17–0.17], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 4096B, q=512): ratio sysv/posix = 0.12× [0.12–0.13], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8192B, q=1): ratio sysv/posix = 1.06× [1.06–1.07], MW p=6.8e-08, Cliff δ=-1.00 (large), winner=shm_sysv
- **SHM throughput** (same_core, 8192B, q=1): ratio sysv/posix = 0.96× [0.96–0.97], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8192B, q=1): ratio sysv/posix = 1.00× [1.00–1.00], MW p=6.0e-03, Cliff δ=0.51 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8192B, q=8): ratio sysv/posix = 0.72× [0.71–0.72], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8192B, q=8): ratio sysv/posix = 0.17× [0.17–0.18], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8192B, q=8): ratio sysv/posix = 0.61× [0.61–0.62], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8192B, q=32): ratio sysv/posix = 0.71× [0.71–0.72], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8192B, q=32): ratio sysv/posix = 0.18× [0.17–0.19], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8192B, q=32): ratio sysv/posix = 0.61× [0.59–0.62], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8192B, q=128): ratio sysv/posix = 0.70× [0.70–0.71], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8192B, q=128): ratio sysv/posix = 0.19× [0.19–0.20], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8192B, q=128): ratio sysv/posix = 0.30× [0.30–0.31], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (diff_l3, 8192B, q=512): ratio sysv/posix = 0.69× [0.68–0.69], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_core, 8192B, q=512): ratio sysv/posix = 0.20× [0.19–0.20], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix
- **SHM throughput** (same_l3, 8192B, q=512): ratio sysv/posix = 0.20× [0.20–0.21], MW p=6.8e-08, Cliff δ=1.00 (large), winner=shm_posix

## Coverage

| benchmark | mechanism | rows | runs |
|---|---|---:|---:|
| fanin_n1 | mq_posix | 60 | 20 |
| fanin_n1 | mq_sysv | 60 | 20 |
| fanin_n16 | mq_posix | 60 | 20 |
| fanin_n16 | mq_sysv | 60 | 20 |
| fanin_n2 | mq_posix | 60 | 20 |
| fanin_n2 | mq_sysv | 60 | 20 |
| fanin_n4 | mq_posix | 60 | 20 |
| fanin_n4 | mq_sysv | 60 | 20 |
| fanin_n8 | mq_posix | 60 | 20 |
| fanin_n8 | mq_sysv | 60 | 20 |
| pairs_n1 | mq_posix | 60 | 20 |
| pairs_n1 | mq_sysv | 60 | 20 |
| pairs_n1 | shm_posix | 60 | 20 |
| pairs_n1 | shm_sysv | 60 | 20 |
| pairs_n2 | mq_posix | 60 | 20 |
| pairs_n2 | mq_sysv | 60 | 20 |
| pairs_n2 | shm_posix | 60 | 20 |
| pairs_n2 | shm_sysv | 60 | 20 |
| pairs_n4 | mq_posix | 60 | 20 |
| pairs_n4 | mq_sysv | 60 | 20 |
| pairs_n4 | shm_posix | 60 | 20 |
| pairs_n4 | shm_sysv | 60 | 20 |
| pairs_n8 | mq_posix | 60 | 20 |
| pairs_n8 | mq_sysv | 60 | 20 |
| pairs_n8 | shm_posix | 60 | 20 |
| pairs_n8 | shm_sysv | 60 | 20 |
| pingpong | mq_posix | 360 | 20 |
| pingpong | mq_sysv | 360 | 20 |
| pingpong | shm_posix | 360 | 20 |
| pingpong | shm_sysv | 360 | 20 |
| throughput | mq_posix | 1800 | 20 |
| throughput | mq_sysv | 1800 | 20 |
| throughput | shm_posix | 1800 | 20 |
| throughput | shm_sysv | 1800 | 20 |

## Notes

- Statistics: medians + bootstrap 95% CIs (10000 resamples).
- Tests: Welch's t (parametric) and Mann-Whitney U (non-parametric).
- Effect size: Cliff's δ (negligible<0.147, small<0.33, medium<0.474, large≥0.474).
- Run order is randomised by run_suite.sh to decouple drift from mechanism.
