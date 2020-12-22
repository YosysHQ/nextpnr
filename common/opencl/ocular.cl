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

/*

See ocular.cc for an overview. References:

[1] Corolla: GPU-Accelerated FPGA Routing Based onSubgraph Dynamic Expansion
Minghua Shen, Guojie Luo
https://ceca.pku.edu.cn/media/lw/137e5df7dec627f988e07d54ff222857.pdf

[2] Work-Efficient Parallel GPU Methods for Single-Source Shortest Paths
Andrew Davidson, Sean Baxter, Michael Garland, John D. Owens
https://escholarship.org/uc/item/8qr166v2

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
    int prev_net_start, prev_net_end;
    int curr_net_start, curr_net_end;
    // current congestion cost
    float curr_cong_cost;
    // near/far threshold
    int near_far_thresh;
    // number of nodes to process per workgroup
    int group_nodes;
    // Total sizes of the dirty and far queues for this net
    int total_far, total_dirty;
    // Last-iteration sizes of the dirty and far queues
    int last_far, last_dirty;
};

struct WorkgroupConfig {
    // net index
    int net;
    // workgroup size
    uint size;
};

#define inf_cost 0x7FFFFFF

// Utility functions
inline bool is_group_leader() {
    return (get_local_id(0) == 0);
}

// Do a binary search to find a start point inside a list of prefix sums
inline int binary_search(__global const uint *sum, int count, int x) {
    int b = 0;
    int e = count - 1;
    int chunk = -1;
    while (b <= e) {
        int i = (b + e) / 2;
        if (x >= (i == 0 ? 0 : sum[i-1])
                && x < sum[i]) {
            chunk = i;
            break;
        } else if ((i == 0 ? 0 : sum[i-1]) > x) {
            e = i - 1;
        } else {
            b = i + 1;
        }
    }
    return chunk;
}

#define LOCK_MUTEX(mutex) do {} while(atomic_cmpxchg(&mutex, 0, 1) == 1)
#define UNLOCK_MUTEX(mutex) do {atomic_xchg(&mutex, 0);} while(0)

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
    __global uint *next_near_queue, __global uint *next_near_queue_count,
    // Next queue - far
    __global uint *next_far_queue, __global uint *next_far_queue_count,
    // Dirtied nodes
    __global uint *dirty_queue, __global uint *dirty_queue_count,
    // Graph state
    __global int *current_cost,
    __global uint *uphill_edge,
    __global const short *bound_count
) {
    int wg_id = get_group_id(0);
    struct WorkgroupConfig wg;
    struct NetConfig net_data;
    __local uint near_queue_offset;
    __local uint far_queue_offset;
    __local uint dirty_queue_offset;

    uint queue_start; // queue start using 'flat' numbering from the beginning of the net
    uint queue_end; // queue start using 'flat' numbering from the beginning of the net
    uint queue_start_chunk; // index into curr_queue_count

    __local int finished_threads;
    // Fetch config
    wg = wg_cfg[wg_id];
    net_data = net_cfg[wg.net];
    near_queue_offset = 0;
    far_queue_offset = 0;
    dirty_queue_offset = 0;
    finished_threads = 0;
    // Do a binary search to find our position within the queue
    queue_start = (wg_id - net_data.prev_net_start) * net_data.group_nodes;
    queue_end = (wg_id - net_data.prev_net_start + 1) * net_data.group_nodes;
    queue_start_chunk =
        binary_search(curr_queue_count + net_data.prev_net_start, (net_data.prev_net_end - net_data.prev_net_start), queue_start);
    barrier(CLK_LOCAL_MEM_FENCE);
    if (queue_start_chunk == -1)
        goto done;
    queue_start_chunk += net_data.prev_net_start;

    // TODO: better work fetching

    /*
        We use an approach here similar to load-balanced partitioning in [2]
        where the queue is processed edge-wise rather than node-wise, each
        thread picking off the next edge from the node at the front of the
        queue.

        This is a bit complex because we have two different offsets to consider.
        The offset into the work queue, which as it was created workgroup-wise will
        have 'gaps' between chunks, the start chunk found by the binary search.
        Then we have the offset into the adjacency list of the current node.
    */

    // number of edges processed so far 
    int acc_edges = 0;
    // edgewise index into queue
    int j = get_local_id(0);
    // start/end offsets into input queue chunk

    int queue_chunk = queue_start_chunk;
    int queue_index = queue_start; // 'flat' index into queue, imagining gaps between chunks don't exist

    int queue_offset_0 = (queue_chunk == net_data.prev_net_start) ? 0 : curr_queue_count[queue_chunk - 1];
    int queue_offset_1 = curr_queue_count[queue_chunk];
    int queue_ptr = queue_chunk * net_data.near_queue_size + (queue_start - queue_offset_0);

    // current explored node
    int curr_node = curr_queue[queue_ptr];
    int curr_cost = current_cost[curr_node];
    // start/end offsets into adjacency list for current node
    int adj_offset_0 = adj_offset[curr_node];
    int adj_offset_1 = adj_offset[curr_node + 1];
    while (true) {
        // Bail out if any queues are at risk of becoming full
        // as this could cause inconsistent behaviour
        barrier(CLK_LOCAL_MEM_FENCE);
        if ((near_queue_offset + wg.size) > net_data.near_queue_size)
            break;
        if ((far_queue_offset + wg.size) > net_data.far_queue_size)
            break;
        if ((dirty_queue_offset + wg.size) > net_data.dirtied_nodes_size)
            break;
        // Search until we get 'our' edge
        while (j >= (acc_edges + (adj_offset_1 - adj_offset_0))) {
            // Check if we've reached the end of our per-group workqueue
            if (queue_index >= queue_end)
                goto done;
            // Fetch the next node in the queue
            ++queue_ptr;
            ++queue_index;
            while (queue_index >= queue_offset_1) {
                ++queue_chunk;
                if (queue_chunk >= net_data.prev_net_end)
                    goto done;
                queue_offset_0 = queue_offset_1;
                queue_offset_1 = curr_queue_count[queue_chunk];
                queue_ptr = queue_chunk * net_data.near_queue_size;
            }

            curr_node = curr_queue[queue_ptr];
            curr_cost = current_cost[curr_node];
            acc_edges += (adj_offset_1 - adj_offset_0);
            adj_offset_0 = adj_offset[curr_node];
            adj_offset_1 = adj_offset[curr_node + 1];
        }
        // Process the edge
        int edge_ptr = adj_offset_0 + (j - acc_edges);
        uint next_node = edge_dst_index[edge_ptr];
        // Move forward 'wg.size' positions in the queue
        j += wg.size;
        // Bounds check
        short next_x = wire_x[next_node];
        short next_y = wire_y[next_node];
        if (next_x < net_data.x0 || next_x > net_data.x1)
            continue;
        if (next_y < net_data.y0 || next_y > net_data.y1)
            continue;
        // TODO: congestion cost factor
        int next_cost = curr_cost + edge_cost[edge_ptr];

        // Avoid the expensive atomic that often won't be needed (dubious?)
        if (current_cost[next_node] < next_cost)
            continue;

        int last_cost = atomic_min(&(current_cost[next_node]), next_cost);
        if (next_cost < last_cost) {
            // Atomic confirms it really is a better path
            uphill_edge[next_node] = edge_ptr;
            if (next_cost < net_data.near_far_thresh) {
                // Lock per-workgroup near output and add
                int offset = atomic_inc(&near_queue_offset);
                next_near_queue[net_data.near_queue_size * wg_id + offset] = next_node;
            } else {
                // Lock per-workgroup far output and add
                int offset = atomic_inc(&far_queue_offset);
                next_far_queue[net_data.far_queue_size * wg_id + offset] = next_node;
            }
            if (last_cost == inf_cost) {
                // Node was never visited before, add it to the dirty queue
                int offset = atomic_inc(&dirty_queue_offset);
                dirty_queue[net_data.dirtied_nodes_size * wg_id + offset] = next_node;
            }
        }
    }
done:
    barrier(CLK_LOCAL_MEM_FENCE);
    atomic_inc(&finished_threads);
    // Wait for all threads to complete
    while (finished_threads != wg.size) {
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (is_group_leader()) {
        // Update queue count
        next_near_queue_count[wg_id] = near_queue_offset;
        next_far_queue_count[wg_id] = far_queue_offset;
        dirty_queue_count[wg_id] = dirty_queue_offset;
    }
    return;
}

__kernel void update_dirty_queue (
    // Configuration
    __global const struct NetConfig *net_cfg,
    __global const struct WorkgroupConfig *wg_cfg,
    // Input
    __global const uint *wg_dirty_queue, __global const uint *wg_dirty_queue_count,
    __global uint *net_dirty_queue, __global const uint *net_dirty_chunks,
    uint dirty_chunk_size, uint net_to_chunk_size
) {
    int wg_id = get_group_id(0);
    struct WorkgroupConfig wg = wg_cfg[wg_id];
    struct NetConfig net_data = net_cfg[wg.net];
    int j = get_local_id(0);
    int queue_offset = 0;
    for (int i = net_data.curr_net_start; i < (wg_id-1); i++) {
        queue_offset += wg_dirty_queue_count[i];
    }
    queue_offset += j;
    int queue_end = wg_dirty_queue_count[wg_id];
    int output_offset = net_data.total_dirty + queue_offset;
    while (queue_offset < queue_end) {
        // Copy a warp-wide chunk from the right place in the input queue to the right place in the chunked output queue
        int output_chunk_idx = output_offset / dirty_chunk_size;
        int output_chunk = net_dirty_chunks[wg.net * net_to_chunk_size + output_chunk_idx];
        net_dirty_queue[output_chunk * dirty_chunk_size + (output_offset % dirty_chunk_size)] = wg_dirty_queue[wg_id * net_data.dirtied_nodes_size + j];
        queue_offset += wg.size;
        j += wg.size;
        output_offset += wg.size;
    }
}

__kernel void reset_visit(
    // Configuration
    __global const struct NetConfig *net_cfg,
    // List of nets to be set as a bitmask
    ulong reset_nets,
    // Structure to reset
    __global int *current_cost,
    // Dirty queue to work from
    __global uint *net_dirty_queue, __global const uint *net_dirty_chunks,
    uint dirty_chunk_size, uint net_to_chunk_size
) {
    int net_id = get_group_id(0);
    if (reset_nets & (1 << (ulong)net_id)) {
        struct NetConfig net_data = net_cfg[net_id];
        int j = get_local_id(0);
        while (j < (net_data.total_dirty + net_data.last_dirty)) {
            int chunk_idx = j / dirty_chunk_size;
            int chunk = net_dirty_chunks[net_id * net_to_chunk_size + chunk_idx];
            int node = net_dirty_queue[chunk * dirty_chunk_size + (j % dirty_chunk_size)];
            current_cost[node] = inf_cost;
            j += get_local_size(0);
        }
    }
}
