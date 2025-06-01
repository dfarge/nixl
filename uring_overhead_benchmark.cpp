#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <liburing.h>
#include <iomanip>

// Configuration
constexpr long TOTAL_TRANSFER_SIZE = 128 * 1024 * 1024; // 128MB
constexpr size_t BUFFER_SIZE = 4 * 1024; // 4KB
constexpr size_t TOTAL_TRANSFERS = TOTAL_TRANSFER_SIZE / BUFFER_SIZE; // 32K
const char* TEST_FILE_BASE = "/tmp/uring_benchmark_file";

// Create a test file with a unique name for a specific uring count
std::string prepare_test_file(size_t uring_count) {
    std::string filename = std::string(TEST_FILE_BASE) + "_" + std::to_string(uring_count);
    
    // Create a test file with some data
    int fd = open(filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        std::cerr << "Failed to create test file: " << strerror(errno) << std::endl;
        exit(1);
    }
    
    std::vector<char> buffer(BUFFER_SIZE, 'A');
    for (size_t i = 0; i < TOTAL_TRANSFERS; i++) {
        if (write(fd, buffer.data(), buffer.size()) != BUFFER_SIZE) {
            std::cerr << "Write failed: " << strerror(errno) << std::endl;
            close(fd);
            exit(1);
        }
    }
    close(fd);
    std::cout << "Created test file: " << filename << " (" 
              << (TOTAL_TRANSFERS * BUFFER_SIZE / (1024*1024)) << " MB)" << std::endl;
              
    return filename;
}

// Actual benchmark function - measures only io_uring operations
double run_uring_benchmark(const std::string& filename, const std::vector<std::vector<void*>>& all_buffers, 
                          size_t num_urings, size_t transfers_per_uring) {
    // Open file for this specific test
    int fd = open(filename.c_str(), O_RDONLY | O_DIRECT);
    if (fd < 0) {
        fd = open(filename.c_str(), O_RDONLY); // Try without O_DIRECT if it fails
        if (fd < 0) {
            std::cerr << "Failed to open test file: " << strerror(errno) << std::endl;
            exit(1);
        }
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t uring_idx = 0; uring_idx < num_urings; uring_idx++) {
        // Create a new io_uring instance with queue depth equal to the number of transfers
        struct io_uring ring;
        struct io_uring_params params = {};
        
        // Use transfers_per_uring as queue depth instead of fixed 32
        int ret = io_uring_queue_init_params(transfers_per_uring, &ring, &params);
        if (ret < 0) {
            std::cerr << "io_uring_queue_init failed: " << strerror(-ret) << std::endl;
            close(fd);
            exit(1);
        }
        
        // Get reference to the pre-allocated buffers for this io_uring
        const auto& buffers = all_buffers[uring_idx];
        
        // Prepare all SQEs before submitting
        for (size_t i = 0; i < transfers_per_uring; i++) {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            if (!sqe) {
                std::cerr << "Failed to get SQE" << std::endl;
                break;
            }
            
            io_uring_prep_read(sqe, fd, buffers[i], BUFFER_SIZE, 
                              (uring_idx * transfers_per_uring + i) * BUFFER_SIZE);
            
            // Use user_data to track which buffer this SQE corresponds to
            sqe->user_data = i;
        }
        
        // Submit all SQEs at once
        ret = io_uring_submit(&ring);
        if (ret != transfers_per_uring) {
            std::cerr << "io_uring_submit failed: " << strerror(-ret) << std::endl;
        } else if (ret != (int)transfers_per_uring) {
            std::cerr << "io_uring_submit only submitted " << ret << " of " 
                      << transfers_per_uring << " requests" << std::endl;
        }
        
        // Wait for all completions using io_uring_for_each_cqe
        size_t completions = 0;
        while (completions < transfers_per_uring) {
            // Process all available completions
            struct io_uring_cqe *cqe;
            unsigned head;
            unsigned count = 0;
            
            io_uring_for_each_cqe(&ring, head, cqe) {
                if (cqe->res < 0) {
                    std::cerr << "Read failed for buffer " << cqe->user_data 
                              << ": " << strerror(-cqe->res) << std::endl;
                }
                count++;
            }
            
            if (count > 0) {
                // Mark all seen CQEs as consumed
                io_uring_cq_advance(&ring, count);
                completions += count;
            }
        }
        
        // Destroy the io_uring instance - the only cleanup we do in the timed section
        io_uring_queue_exit(&ring);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    
    // Close file descriptor for this test
    close(fd);
    
    return elapsed.count();
}

int main(int argc, char** argv) {
    // Default test configurations
    std::vector<size_t> uring_counts = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    
    std::cout << "Benchmark: Measuring io_uring creation/destruction overhead" << std::endl;
    std::cout << "Total transfers: " << TOTAL_TRANSFERS << std::endl;
    std::cout << "Buffer size: " << BUFFER_SIZE << " bytes" << std::endl;
    std::cout << "Total transfer size: " << (TOTAL_TRANSFER_SIZE / (1024*1024)) << " MB (" 
              << (TOTAL_TRANSFER_SIZE / (1024.0*1024*1024)) << " GB)" << std::endl;
    std::cout << "Mode: BATCH (queue depth = transfers_per_uring, all SQEs submitted at once)" << std::endl;
    std::cout << "Note: Only io_uring operations are timed (buffer allocation and file I/O setup excluded)" << std::endl;
    std::cout << "Using a separate file for each uring count to avoid caching effects" << std::endl;
    std::cout << std::endl;
    
    // Run benchmarks and collect results
    struct Result {
        size_t num_urings;
        size_t transfers_per_uring;
        double time;
        double throughput;
        std::string filename;
    };
    
    std::vector<Result> results;
    std::vector<std::vector<std::vector<void*>>> all_test_buffers;
    
    // Pre-allocate all buffers for all tests
    for (size_t num_urings : uring_counts) {
        if (TOTAL_TRANSFERS % num_urings != 0) {
            std::cout << "Skipping " << num_urings << " (not divisible)" << std::endl;
            continue;
        }
        
        size_t transfers_per_uring = TOTAL_TRANSFERS / num_urings;
        
        // Create a unique test file for this uring count
        std::string filename = prepare_test_file(num_urings);
        
        // Allocate all buffers for this test
        std::vector<std::vector<void*>> test_buffers;
        test_buffers.resize(num_urings);
        
        for (size_t uring_idx = 0; uring_idx < num_urings; uring_idx++) {
            test_buffers[uring_idx].resize(transfers_per_uring);
            
            for (size_t i = 0; i < transfers_per_uring; i++) {
                void* buf = nullptr;
                if (posix_memalign(&buf, 4096, BUFFER_SIZE) != 0) {
                    std::cerr << "Failed to allocate aligned memory" << std::endl;
                    exit(1);
                }
                test_buffers[uring_idx][i] = buf;
            }
        }
        
        all_test_buffers.push_back(std::move(test_buffers));
        
        // Run the actual benchmark with the specific file for this test
        double time = run_uring_benchmark(filename, all_test_buffers.back(), num_urings, transfers_per_uring);
        double throughput = (TOTAL_TRANSFERS * BUFFER_SIZE) / (time * 1024 * 1024);
        
        results.push_back({num_urings, transfers_per_uring, time, throughput, filename});
    }
    
    // Calculate overhead based on results
    if (!results.empty()) {
        double baseline_time = results[0].time;
        
        // Print headers
        std::cout << std::left << std::setw(10) << "URings" 
                  << std::setw(20) << "Transfers/URing" 
                  << std::setw(15) << "Time(s)" 
                  << std::setw(20) << "Throughput(MB/s)"
                  << std::setw(20) << "Est. Overhead(ms)" 
                  << std::endl;
        
        std::cout << std::string(80, '-') << std::endl;
        
        // Print results
        for (const auto& r : results) {
            // Calculate estimated overhead
            double estimated_overhead_ms = 0.0;
            if (r.num_urings > 1) {
                // For each uring setup, the overhead is (total time - expected time for transfers) / num_urings
                // Expected time for transfers is based on the baseline (1 uring case)
                estimated_overhead_ms = (r.time - baseline_time) * 1000.0;
            }
            
            std::cout << std::left 
                      << std::setw(10) << r.num_urings 
                      << std::setw(20) << r.transfers_per_uring 
                      << std::setw(15) << std::fixed << std::setprecision(6) << r.time 
                      << std::setw(20) << std::fixed << std::setprecision(4) << r.throughput;
            
            if (r.num_urings == 1) {
                std::cout << std::setw(20) << "baseline";
            } else {
                std::cout << std::setw(20) << std::fixed << std::setprecision(3) << estimated_overhead_ms;
            }
            
            std::cout << std::endl;
        }
    }
    
    // Free all allocated buffers
    for (auto& test_buffers : all_test_buffers) {
        for (auto& buffers : test_buffers) {
            for (void* buf : buffers) {
                free(buf);
            }
        }
    }
    
    // Clean up all test files
    for (const auto& result : results) {
        unlink(result.filename.c_str());
    }
    
    return 0;
} 