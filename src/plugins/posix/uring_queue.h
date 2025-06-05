/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef URING_QUEUE_H
#define URING_QUEUE_H

#include <liburing.h>
#include <vector>
#include <memory>
#include "posix_queue.h"
#include <absl/strings/str_format.h>

// Forward declare Error class
class nixlPosixBackendReqH;

// Type definition for io_uring prep functions
typedef void (*io_uring_prep_func_t)(struct io_uring_sqe*, int, const void*, unsigned int, __u64);

class Uring {
    private:
        struct io_uring uring;        // The io_uring instance for async I/O operations
        int num_entries;              // Total number of entries expected in this ring
        int num_completed;            // Number of completed operations so far
        io_uring_prep_func_t prep_op; // Pointer to prep function

    public:
        Uring(int num_entries, io_uring_params params, io_uring_prep_func_t prep_op);
        ~Uring();

        // Delete copy and move operations to prevent accidental copying of kernel resources
        Uring(const Uring&) = delete;
        Uring& operator=(const Uring&) = delete;
        Uring(Uring&&) = delete;
        Uring& operator=(Uring&&) = delete;

        nixl_status_t submit();
        nixl_status_t checkCompleted();
        nixl_status_t prepIO(int fd, void* buf, size_t len, off_t offset);
};

struct UringQueueParams {
    int num_entries;
    const struct io_uring_params& uring_params;
    nixl_xfer_op_t operation;
    const nixl_b_params_t& queue_params;
};

class UringQueue : public nixlPosixQueue {
    private:
        static constexpr size_t default_max_uring_depth = std::numeric_limits<size_t>::max();
        std::vector<std::unique_ptr<Uring>> urings;
        size_t max_uring_depth = default_max_uring_depth;
        size_t num_submitted = 0;

        // Initialize the queue with the given parameters
        nixl_status_t init(int num_entries, const struct io_uring_params& params);

    public:
        UringQueue(const UringQueueParams& params);
        ~UringQueue() = default;

        // Delete copy and move operations to prevent accidental copying of kernel resources
        UringQueue(const UringQueue&) = delete;
        UringQueue& operator=(const UringQueue&) = delete;
        UringQueue(UringQueue&&) = delete;
        UringQueue& operator=(UringQueue&&) = delete;

        nixl_status_t submit() override;
        nixl_status_t checkCompleted() override;
        nixl_status_t prepIO(int fd, void* buf, size_t len, off_t offset) override;
};

#endif // URING_QUEUE_H
