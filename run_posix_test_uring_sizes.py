#!/usr/bin/env python3

import argparse
import subprocess
import os
import time
import json
import re
from datetime import datetime

def parse_args():
    parser = argparse.ArgumentParser(description='Run nixl_posix_test with different max uring sizes')
    parser.add_argument('-e', '--executable', type=str, default='/tmp/nixl/test/unit/plugins/posix/nixl_posix_test',
                      help='Path to nixl_posix_test executable (default: /tmp/nixl/test/unit/plugins/posix/nixl_posix_test)')
    parser.add_argument('-n', '--num-transfers', type=int, default=32768,
                      help='Number of transfers (default: 32768)')
    parser.add_argument('-s', '--transfer-size', type=str, default='512K',
                      help='Transfer size (e.g., 512K, 1M, 2G) (default: 512K)')
    parser.add_argument('-d', '--test-dir', type=str, default='tmp/testfiles',
                      help='Test files directory path (default: tmp/testfiles)')
    parser.add_argument('-D', '--direct-io', action='store_true',
                      help='Use O_DIRECT for file I/O')
    parser.add_argument('-o', '--output', type=str, default='posix_test_uring_sizes_results.json',
                      help='Output JSON file for results (default: posix_test_uring_sizes_results.json)')
    parser.add_argument('-w', '--wait', type=int, default=0,
                      help='Wait time in seconds between tests (default: 0)')
    parser.add_argument('-l', '--log-dir', type=str, default='test_logs_uring_sizes',
                      help='Directory for test output logs (default: test_logs_uring_sizes)')
    return parser.parse_args()

def parse_size(size_str):
    """Convert size string (e.g., '512K', '1M', '2G') to bytes."""
    units = {'K': 1024, 'M': 1024*1024, 'G': 1024*1024*1024}
    size_str = size_str.upper()
    if size_str[-1] in units:
        return int(size_str[:-1]) * units[size_str[-1]]
    return int(size_str)

def extract_speeds(output):
    """Extract write and read speeds using regex."""
    write_speed = None
    read_speed = None
    
    # Pattern to match speed values like "Speed: 1.23 GB/s"
    speed_pattern = r"Speed: ([\d.]+) GB/s"
    
    # Find all speed matches
    speed_matches = re.findall(speed_pattern, output)
    
    if len(speed_matches) >= 1:
        write_speed = float(speed_matches[0])
    if len(speed_matches) >= 2:
        read_speed = float(speed_matches[1])
    
    return write_speed, read_speed

def run_test(args, max_uring_size, iteration):
    """Run a single test with specified max uring size and return the results."""
    cmd = [args.executable,
           '-n', str(args.num_transfers),
           '-s', str(parse_size(args.transfer_size)),
           '-d', args.test_dir,
           '-U']  # Always use io_uring backend
    
    # Set max uring depth
    cmd.extend(['-m', str(max_uring_size)])
    
    if args.direct_io:
        cmd.append('-D')

    # Create log file for this test
    uring_size_str = "unlimited" if max_uring_size == 0 else str(max_uring_size)
    log_file = os.path.join(args.log_dir, f'test_iter_{iteration}_uring_size_{uring_size_str}.log')
    with open(log_file, 'w') as f:
        f.write(f"Command: {' '.join(cmd)}\n")
        f.write(f"Iteration: {iteration}\n")
        f.write(f"Max uring size: {uring_size_str}\n")
        f.write(f"Started at: {datetime.now().isoformat()}\n")
        f.write("-" * 80 + "\n")

    print(f"\nRunning test with max uring size: {uring_size_str}")
    print(f"Command: {' '.join(cmd)}")
    start_time = time.time()
    
    # Run the test and capture output
    with open(log_file, 'a') as f:
        result = subprocess.run(cmd, stdout=f, stderr=f, text=True)
    
    end_time = time.time()

    # Read the log file to extract speeds
    with open(log_file, 'r') as f:
        output = f.read()
    
    write_speed, read_speed = extract_speeds(output)

    # Append completion time to log
    with open(log_file, 'a') as f:
        f.write("-" * 80 + "\n")
        f.write(f"Completed at: {datetime.now().isoformat()}\n")
        f.write(f"Duration: {end_time - start_time:.2f} seconds\n")
        f.write(f"Exit code: {result.returncode}\n")

    return {
        'iteration': iteration,
        'max_uring_size': max_uring_size,
        'max_uring_size_str': uring_size_str,
        'timestamp': datetime.now().isoformat(),
        'command': ' '.join(cmd),
        'exit_code': result.returncode,
        'duration': end_time - start_time,
        'write_speed_gbps': write_speed,
        'read_speed_gbps': read_speed,
        'log_file': log_file
    }

def main():
    args = parse_args()
    
    # Configuration
    NUM_ITERATIONS = 2
    
    # Create test and log directories if they don't exist
    os.makedirs(args.test_dir, exist_ok=True)
    os.makedirs(args.log_dir, exist_ok=True)
    
    # Check if executable exists
    if not os.path.isfile(args.executable):
        print(f"Error: Executable not found at {args.executable}")
        print("Please specify the correct path using -e/--executable")
        return
    
    # Define the max uring sizes to test
    # Start with unlimited (0), then powers of 2 from 128 to 8192
    max_uring_sizes = [0]  # unlimited first
    size = 128
    while size <= 8192:
        max_uring_sizes.append(size)
        size *= 2
    
    results = []
    
    # Calculate total transfer size
    transfer_size_bytes = parse_size(args.transfer_size)
    total_transfer_size_bytes = transfer_size_bytes * args.num_transfers
    
    # Format total transfer size for display
    if total_transfer_size_bytes >= 1024**3:  # GB
        total_size_str = f"{total_transfer_size_bytes / (1024**3):.2f} GB"
    elif total_transfer_size_bytes >= 1024**2:  # MB
        total_size_str = f"{total_transfer_size_bytes / (1024**2):.2f} MB"
    elif total_transfer_size_bytes >= 1024:  # KB
        total_size_str = f"{total_transfer_size_bytes / 1024:.2f} KB"
    else:
        total_size_str = f"{total_transfer_size_bytes} bytes"
    
    print(f"Starting posix test with different max uring sizes")
    print(f"Configuration:")
    print(f"- Executable: {args.executable}")
    print(f"- Number of transfers: {args.num_transfers}")
    print(f"- Transfer size: {args.transfer_size}")
    print(f"- Total transfer size: {total_size_str}")
    print(f"- Test directory: {args.test_dir}")
    print(f"- Log directory: {args.log_dir}")
    print(f"- Direct I/O: {'enabled' if args.direct_io else 'disabled'}")
    print(f"- Backend: io_uring (forced)")
    print(f"- Max uring sizes to test: {[('unlimited' if s == 0 else s) for s in max_uring_sizes]}")
    print(f"- Iterations: {NUM_ITERATIONS}")
    
    # Outer loop for iterations
    for iteration in range(1, NUM_ITERATIONS + 1):
        print(f"\n{'='*20} ITERATION {iteration}/{NUM_ITERATIONS} {'='*20}")
        
        for i, max_uring_size in enumerate(max_uring_sizes):
            print(f"\n=== Iteration {iteration}, Test {i+1}/{len(max_uring_sizes)} ===")
            result = run_test(args, max_uring_size, iteration)
            results.append(result)
            
            if result['exit_code'] != 0:
                print(f"Test failed with exit code {result['exit_code']}")
                print(f"See log file for details: {result['log_file']}")
                break
            
            # Print speed results if available
            if result['write_speed_gbps'] is not None:
                print(f"Write speed: {result['write_speed_gbps']:.2f} GB/s")
            if result['read_speed_gbps'] is not None:
                print(f"Read speed: {result['read_speed_gbps']:.2f} GB/s")
                
            if i < len(max_uring_sizes) - 1 and args.wait > 0:
                print(f"Waiting {args.wait} seconds before next test...")
                time.sleep(args.wait)
        
        # Break out of outer loop if there was a failure
        if results[-1]['exit_code'] != 0:
            break
        
        # Wait between iterations if specified
        if iteration < NUM_ITERATIONS and args.wait > 0:
            print(f"Waiting {args.wait} seconds before next iteration...")
            time.sleep(args.wait)
    
    # Calculate summary statistics for JSON output
    from collections import defaultdict
    results_by_size = defaultdict(list)
    
    for result in results:
        results_by_size[result['max_uring_size']].append(result)
    
    summary_stats = {}
    for max_uring_size in max_uring_sizes:
        if max_uring_size not in results_by_size:
            continue
            
        size_results = results_by_size[max_uring_size]
        uring_size_str = "unlimited" if max_uring_size == 0 else str(max_uring_size)
        
        # Calculate statistics
        write_speeds = [r['write_speed_gbps'] for r in size_results if r['write_speed_gbps'] is not None]
        read_speeds = [r['read_speed_gbps'] for r in size_results if r['read_speed_gbps'] is not None]
        passed_tests = sum(1 for r in size_results if r['exit_code'] == 0)
        total_tests = len(size_results)
        
        summary_stats[uring_size_str] = {
            'max_uring_size': max_uring_size,
            'total_tests': total_tests,
            'passed_tests': passed_tests,
            'write_speeds': {
                'average': sum(write_speeds)/len(write_speeds) if write_speeds else None,
                'minimum': min(write_speeds) if write_speeds else None,
                'maximum': max(write_speeds) if write_speeds else None,
                'count': len(write_speeds)
            },
            'read_speeds': {
                'average': sum(read_speeds)/len(read_speeds) if read_speeds else None,
                'minimum': min(read_speeds) if read_speeds else None,
                'maximum': max(read_speeds) if read_speeds else None,
                'count': len(read_speeds)
            }
        }
    
    # Save results to JSON file
    with open(args.output, 'w') as f:
        json.dump({
            'test_config': vars(args),
            'max_uring_sizes_tested': max_uring_sizes,
            'summary_statistics': summary_stats,
            'results': results
        }, f, indent=2)
    
    print(f"\nResults saved to {args.output}")
    
    # Print summary table with averages
    print(f"\n=== PERFORMANCE SUMMARY (AVERAGES ACROSS {NUM_ITERATIONS} ITERATIONS) ===")
    print(f"{'Max Uring Size':<15} {'Avg Write (GB/s)':<17} {'Avg Read (GB/s)':<16} {'Min Write':<11} {'Max Write':<11} {'Min Read':<10} {'Max Read':<10} {'Pass/Total'}")
    print("-" * 110)
    
    for max_uring_size in max_uring_sizes:
        uring_size_str = "unlimited" if max_uring_size == 0 else str(max_uring_size)
        if uring_size_str not in summary_stats:
            continue
            
        stats = summary_stats[uring_size_str]
        
        # Format averages and ranges
        avg_write = f"{stats['write_speeds']['average']:.2f}" if stats['write_speeds']['average'] is not None else "N/A"
        avg_read = f"{stats['read_speeds']['average']:.2f}" if stats['read_speeds']['average'] is not None else "N/A"
        min_write = f"{stats['write_speeds']['minimum']:.2f}" if stats['write_speeds']['minimum'] is not None else "N/A"
        max_write = f"{stats['write_speeds']['maximum']:.2f}" if stats['write_speeds']['maximum'] is not None else "N/A"
        min_read = f"{stats['read_speeds']['minimum']:.2f}" if stats['read_speeds']['minimum'] is not None else "N/A"
        max_read = f"{stats['read_speeds']['maximum']:.2f}" if stats['read_speeds']['maximum'] is not None else "N/A"
        pass_ratio = f"{stats['passed_tests']}/{stats['total_tests']}"
        
        print(f"{uring_size_str:<15} {avg_write:<17} {avg_read:<16} {min_write:<11} {max_write:<11} {min_read:<10} {max_read:<10} {pass_ratio}")
    
    # Print detailed results
    print(f"\n=== DETAILED RESULTS (ALL ITERATIONS) ===")
    print(f"{'Iteration':<10} {'Max Uring Size':<15} {'Write Speed (GB/s)':<18} {'Read Speed (GB/s)':<17} {'Status'}")
    print("-" * 75)
    
    for result in results:
        iteration = result['iteration']
        uring_size_str = result['max_uring_size_str']
        write_speed = f"{result['write_speed_gbps']:.2f}" if result['write_speed_gbps'] is not None else "N/A"
        read_speed = f"{result['read_speed_gbps']:.2f}" if result['read_speed_gbps'] is not None else "N/A"
        status = "PASS" if result['exit_code'] == 0 else "FAIL"
        
        print(f"{iteration:<10} {uring_size_str:<15} {write_speed:<18} {read_speed:<17} {status}")

if __name__ == '__main__':
    main() 