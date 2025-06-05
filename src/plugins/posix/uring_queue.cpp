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

#include "uring_queue.h"
#include <liburing.h>
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <limits>
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "common/nixl_log.h"

//TODO REMOVE:
#include <iostream>

namespace {
    // // Log completion percentage at regular intervals (every log_percent_step percent)
    // void logOnPercentStep(unsigned int completed, unsigned int total) {
    //     constexpr unsigned int log_percent_step = 10;
    //     static_assert(log_percent_step >= 1 && log_percent_step <= 100, "log_percent_step must be in [1, 100]");

    //     if (total == 0) {
    //         NIXL_ERROR << "Tried to log completion percentage with total == 0";
    //         return;
    //     }
    //     // Only log at each percentage step
    //     if (completed % (total / log_percent_step) == 0) {
    //         NIXL_DEBUG << absl::StrFormat("Queue progress: %.1f%% complete",
    //                                       (completed * 100.0 / total));
    //     }
    // }

    std::string stringifyUringFeatures(unsigned int features) {
        static const std::unordered_map<unsigned int, std::string> feature_map = {
            {IORING_FEAT_SQPOLL_NONFIXED, "SQPOLL"},
            {IORING_FEAT_FAST_POLL, "IOPOLL"}
        };

        std::vector<std::string> enabled;
        for (unsigned int bits = features; bits; bits &= (bits - 1)) { // step through each set bit
            unsigned int bit = bits & -bits; // isolate lowest set bit
            auto it = feature_map.find(bit);
            if (it != feature_map.end()) {
                enabled.push_back(it->second);
            }
        }
        return enabled.empty() ? "none" : absl::StrJoin(enabled, ", ");
    }
}

// We pass a copy of the params to the Uring constructor because the io_uring_queue_init_params
// function requires a mutable copy of the params
Uring::Uring(int num_entries, io_uring_params params, io_uring_prep_func_t prep_op)
    : num_entries(num_entries)
    , num_completed(0)
    , prep_op(prep_op)
{
    if (num_entries <= 0) {
        throw std::invalid_argument("Invalid number of entries for Uring");
    }

    if (io_uring_queue_init_params(num_entries, &uring, &params) < 0) {
        throw std::runtime_error(absl::StrFormat("Failed to initialize io_uring instance: %s", strerror(errno)));
    }

    NIXL_INFO << absl::StrFormat("io_uring features: %s", stringifyUringFeatures(params.features));
}

Uring::~Uring() {
    io_uring_queue_exit(&uring);
}

nixl_status_t Uring::submit() {
    int ret = io_uring_submit(&uring);
    if (ret != num_entries) {
        if (ret < 0) {
            NIXL_ERROR << absl::StrFormat("io_uring submit failed: %s", strerror(-ret));
        }
        else {
            NIXL_ERROR << absl::StrFormat("io_uring submit failed. Partial submission: %d/%d", num_entries, ret);
        }
        return NIXL_ERR_BACKEND;
    }
    return NIXL_IN_PROG;
}

nixl_status_t Uring::checkCompleted() {
    if (num_completed == num_entries) {
        return NIXL_SUCCESS;
    }

    // Process all available completions
    struct io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    // Get completion events
    io_uring_for_each_cqe(&uring, head, cqe) {
        int res = cqe->res;
        if (res < 0) {
            NIXL_ERROR << absl::StrFormat("IO operation failed: %s", strerror(-res));
            return NIXL_ERR_BACKEND;
        }
        count++;
    }

    // Mark all seen
    io_uring_cq_advance(&uring, count);
    num_completed += count;

    return (num_completed == num_entries) ? NIXL_SUCCESS : NIXL_IN_PROG;
}

nixl_status_t Uring::prepIO(int fd, void* buf, size_t len, off_t offset) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&uring);
    if (!sqe) {
        NIXL_ERROR << "Failed to get io_uring submission queue entry";
        return NIXL_ERR_BACKEND;
    }

    prep_op(sqe, fd, buf, len, offset);
    return NIXL_SUCCESS;
}

UringQueue::UringQueue(const UringQueueParams& params) {
    io_uring_prep_func_t prep_op = params.operation == NIXL_READ ?
        reinterpret_cast<io_uring_prep_func_t>(io_uring_prep_read) :
        reinterpret_cast<io_uring_prep_func_t>(io_uring_prep_write);

    if (params.queue_params.count("max_uring_depth") > 0) {
        max_uring_depth = std::stoul(params.queue_params.at("max_uring_depth"));
    }

    const size_t num_urings = (max_uring_depth == std::numeric_limits<size_t>::max()) 
        ? 1  // Unlimited depth means we only need 1 uring
        : (params.num_entries + max_uring_depth - 1) / max_uring_depth;

    urings.reserve(num_urings);

    size_t num_entries = params.num_entries;
    while (num_entries > 0) {
        size_t uring_num_entries = std::min(num_entries, max_uring_depth);
        urings.emplace_back(std::make_unique<Uring>(uring_num_entries, params.uring_params, prep_op));
        num_entries -= uring_num_entries;
    }
}

nixl_status_t UringQueue::submit() {
    for (auto& uring : urings) {
        nixl_status_t status = uring->submit();
        if (status != NIXL_IN_PROG) {
            return status;
        }
    }
    return NIXL_IN_PROG;
}

nixl_status_t UringQueue::checkCompleted() {
    nixl_status_t status = NIXL_SUCCESS;
    for (auto& uring : urings) {
        nixl_status_t uring_status = uring->checkCompleted();
        if (uring_status != NIXL_SUCCESS) {
            if (uring_status == NIXL_IN_PROG) {
                status = NIXL_IN_PROG;
            } else {
                return uring_status;
            }
        }
    }
    return status;
}

nixl_status_t UringQueue::prepIO(int fd, void* buf, size_t len, off_t offset) {
    size_t uring_idx = num_submitted / max_uring_depth;

    auto status = urings[uring_idx]->prepIO(fd, buf, len, offset);
    if (status == NIXL_SUCCESS) {
        num_submitted++;
    }
    return status;
}
