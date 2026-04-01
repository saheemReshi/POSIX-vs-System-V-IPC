import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import os
import glob

def load_data():
    """Finds and concatenates the POSIX and System V CSV files."""
    posix_files = glob.glob('../posix/results/*.csv')
    sysv_files = glob.glob('../system_V/results/*.csv')
    
    all_files = posix_files + sysv_files
    if not all_files:
        print("No CSV files found in ./posix/results/ or ./system_V/results/")
        return None

    df_list = [pd.read_csv(f) for f in all_files]
    df = pd.concat(df_list, ignore_index=True)
    
    # Standardize column names just in case of whitespace
    df.columns = df.columns.str.strip()
    return df

def plot_pingpong_latency(df, out_dir):
    """Plots Ping-Pong average latency vs. message size."""
    plt.figure(figsize=(14, 6))
    pp_df = df[df['benchmark'] == 'pingpong'].copy()
    
    # Plot Same Core
    plt.subplot(1, 2, 1)
    same_core = pp_df[pp_df['placement'].str.contains('same', na=False)]
    if not same_core.empty:
        sns.lineplot(data=same_core, x='msg_size_bytes', y='avg_ns', hue='mechanism', marker='o')
        plt.xscale('log', base=2)
        plt.yscale('log', base=10)
        plt.title('Ping-Pong Latency (Same Core)')
        plt.xlabel('Message Size (Bytes)')
        plt.ylabel('Average Latency (ns)')

    # Plot Cross Core
    plt.subplot(1, 2, 2)
    diff_core = pp_df[pp_df['placement'].str.contains('diff|cross', na=False)]
    if not diff_core.empty:
        sns.lineplot(data=diff_core, x='msg_size_bytes', y='avg_ns', hue='mechanism', marker='o')
        plt.xscale('log', base=2)
        plt.yscale('log', base=10)
        plt.title('Ping-Pong Latency (Cross Core)')
        plt.xlabel('Message Size (Bytes)')
        plt.ylabel('Average Latency (ns)')

    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, '01_latency_comparison.png'), dpi=300)
    plt.close()

def plot_peak_throughput(df, out_dir):
    """Plots peak throughput (MB/s) vs. message size."""
    plt.figure(figsize=(10, 6))
    tp_df = df[df['benchmark'] == 'throughput'].copy()
    
    if tp_df.empty:
        return
        
    sns.barplot(data=tp_df, x='msg_size_bytes', y='throughput_MB_s', hue='mechanism', errorbar=None)
    plt.title('Peak Bandwidth / Throughput')
    plt.xlabel('Message Size (Bytes)')
    plt.ylabel('Throughput (MB/s)')
    plt.savefig(os.path.join(out_dir, '02_throughput_comparison.png'), dpi=300)
    plt.close()

def plot_fanin_scalability(df, out_dir):
    """Plots Fan-In (C1) throughput vs. Number of Producers."""
    plt.figure(figsize=(10, 6))
    # Filter for C1 fanin tests. We'll plot the 64-byte or 256-byte message size
    c1_df = df[df['benchmark'].str.startswith('fanin') | df['placement'].str.contains('fanin')].copy()
    
    if c1_df.empty:
        return

    # Let's isolate the 64-byte payload for a clean line chart
    c1_64b = c1_df[c1_df['msg_size_bytes'] == 64]
    
    if not c1_64b.empty:
        sns.lineplot(data=c1_64b, x='n_procs', y='throughput_msg_s', hue='mechanism', marker='s')
        plt.title('Fan-In Scalability (N Producers -> 1 Consumer, 64B Payload)')
        plt.xlabel('Number of Producers (N)')
        plt.ylabel('Throughput (Messages / sec)')
        plt.xticks(c1_64b['n_procs'].unique())
        plt.savefig(os.path.join(out_dir, '03_scalability_fanin.png'), dpi=300)
    plt.close()

def plot_parallel_pairs_scalability(df, out_dir):
    """Plots Parallel Pairs (C2) 99th percentile latency vs. Number of Pairs."""
    plt.figure(figsize=(10, 6))
    c2_df = df[df['benchmark'].str.startswith('pairs') | df['placement'].str.contains('pairs')].copy()
    
    if c2_df.empty:
        return

    # Isolate the 64-byte payload
    c2_64b = c2_df[c2_df['msg_size_bytes'] == 64].copy()
    
    if not c2_64b.empty:
        # In the pairs test, n_procs is usually N pairs * 2. 
        # We calculate the number of pairs to make the X-axis intuitive.
        c2_64b['n_pairs'] = c2_64b['n_procs'] / 2
        sns.lineplot(data=c2_64b, x='n_pairs', y='p99_ns', hue='mechanism', marker='^')
        plt.title('Parallel Pairs Scalability (99th Percentile Latency, 64B Payload)')
        plt.xlabel('Number of Concurrent Pairs')
        plt.ylabel('p99 Latency (ns)')
        plt.xticks(c2_64b['n_pairs'].unique())
        plt.savefig(os.path.join(out_dir, '04_scalability_pairs.png'), dpi=300)
    plt.close()

def main():
    print("Loading benchmark data...")
    df = load_data()
    if df is None:
        return

    out_dir = './visualizations'
    os.makedirs(out_dir, exist_ok=True)
    
    # Set seaborn styling for academic/professional look
    sns.set_theme(style="whitegrid", context="paper", font_scale=1.2)

    print("Generating Latency charts...")
    plot_pingpong_latency(df, out_dir)
    
    print("Generating Throughput charts...")
    plot_peak_throughput(df, out_dir)
    
    print("Generating Scalability charts...")
    plot_fanin_scalability(df, out_dir)
    plot_parallel_pairs_scalability(df, out_dir)
    
    print(f"Success! All charts have been saved to '{out_dir}/'.")

if __name__ == "__main__":
    main()
