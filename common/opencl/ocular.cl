/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

// Data structures - keep in sync with ocular.cc
struct NetConfig {
    // Net bounding box
    short x0, y0, x1, y1;
    // max size of the near and far queue
    int near_queue_size, far_queue_size;
    // max size of the dirtied nodes structure
    int dirtied_nodes_size;
    // start and end workgroup offsets for the net
    int net_start, net_end;
    // current congestion cost
    float curr_cong_cost;
};

struct WorkgroupConfig {
    int net;
};

// Utility functions
inline bool is_leader() {
	return get_local_id(0) == 0;
}

// Kernels for the OCuLaR GPGPU router

__kernel void ocular_route (
    // Configuration
    __global const struct NetConfig *net_cfg,
    __global const struct WorkgroupConfig *wg_cfg,
    // Routing graph - see descriptions in ocular.cc
    __global const short *wire_x,  __global const short *wire_y,
    __global const uint *adj_offset,
    __global const uint *edge_dst_index,
    __global const int *edge_cost,
    // Current queue
    __global const uint *curr_queue, __global const uint *curr_queue_count,
    // Next queue - near
    __global uint *next_near_queue, __global const uint *next_near_queue_count,
    // Next queue - far
    __global uint *next_far_queue, __global const uint *next_far_queue_count,
    // Graph state
    __global int *current_cost,
    __global uint *uphill_edge,
    __global const uchar *bound_count
) {
	__local struct WorkgroupConfig wg;
	__local struct NetConfig net_data;
	if (is_leader()) {
		// Fetch config
		wg = wg_cfg[get_global_id(0)];
		net_data = net_cfg[wg.net];
		// Binary search for offsets
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	// Fetch work
	// Update queue as needed
	// Check for completion
}
