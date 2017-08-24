#include "graph.h"
#include <list.h>
#include <array.h>
#include <queue.h>
#include <assert.h>


struct graph_s {
    Array *nodes;       // array of nodes
    Array *edges;       // array of adjacency lists; the i-th list contains
                        // outgoing edges from i-th node in nodes
    size_t n_nodes;     // number of vertices
};


enum cc_stat graph_new(Graph **graph)
{
    *graph = malloc(sizeof(Graph));
    if (*graph == NULL)
        return CC_ERR_ALLOC;
    memset(*graph, 0, sizeof(Graph));
    enum cc_stat ret = array_new(&(*graph)->nodes);
    if (ret != CC_OK)
        return ret;
    return array_new(&(*graph)->edges);
}


void graph_destroy(Graph *graph)
{
    array_destroy(graph->nodes);
    ArrayIter edges_iter;
    array_iter_init(&edges_iter, graph->edges);
    List *edges_list = NULL;
    while (array_iter_next(&edges_iter, (void **) &edges_list) != CC_ITER_END) {
        list_destroy(edges_list);
    }
    array_destroy(graph->edges);
    free(graph);
}


enum cc_stat graph_add(Graph *graph, uint64_t *from, uint64_t *to)
{
    enum cc_stat ret;
    ArrayIter nodes_iter;
    array_iter_init(&nodes_iter, graph->nodes);
    uint64_t *value = NULL;
    while (array_iter_next(&nodes_iter, (void **) &value) != CC_ITER_END) {
        if (*value != *from)
            continue;
        size_t from_node_idx = array_iter_index(&nodes_iter);
        List *from_nodes_list = NULL;
        ret = array_get_at(graph->edges, from_node_idx, (void **) &from_nodes_list);
        if (ret != CC_OK)
            return ret;

        ListIter edges_iter;
        list_iter_init(&edges_iter, from_nodes_list);
        value = NULL;
        while (list_iter_next(&edges_iter, (void **) &value) != CC_ITER_END) {
            if (*value == *to)
                return CC_GRAPH_BOTH_EXIST;
        }

        ret = list_add(from_nodes_list, to);
        if (ret != CC_OK)
            return ret;
        graph->n_nodes++;
        return CC_GRAPH_FROM_EXISTS;
    }

    List *new_edge_list;
    if ((ret = list_new(&new_edge_list)) != CC_OK)
        return ret;
    if ((ret = list_add(new_edge_list, to)) != CC_OK)
        return ret;
    if ((ret = array_add(graph->nodes, from)) != CC_OK)
        return ret;
    graph->n_nodes++;
    if ((ret = array_add(graph->edges, new_edge_list)) != CC_OK)
        return ret;
    graph->n_nodes++;
    return CC_OK;
}


void graph_foreach(Graph *graph, void *data, void (*fn)(uint64_t *, uint64_t **, size_t, void*))
{
    ArrayIter nodes_iter;
    array_iter_init(&nodes_iter, graph->nodes);
    uint64_t *node = NULL;
    while (array_iter_next(&nodes_iter, (void **) &node) != CC_ITER_END) {
        size_t from_node_idx = array_iter_index(&nodes_iter);
        List *from_nodes_list = NULL;
        assert(array_get_at(graph->edges, from_node_idx, (void **) &from_nodes_list) == CC_OK);

        ListIter edges_iter;
        list_iter_init(&edges_iter, from_nodes_list);

        size_t connections_size = list_size(from_nodes_list);
        uint64_t **connections = malloc(sizeof(uint64_t **) * connections_size);
        uint64_t *edge_value = NULL;
        uint64_t **connections_ptr = connections;
        while (list_iter_next(&edges_iter, (void **) &edge_value) != CC_ITER_END) {
            *connections_ptr = edge_value;
            connections_ptr++;
        }

        fn(node, connections, connections_size, data);
        free(connections);
    }
}


size_t graph_nodes(Graph *graph)
{
    return graph->n_nodes;
}


size_t graph_edges(Graph *graph)
{
    ArrayIter nodes_iter;
    array_iter_init(&nodes_iter, graph->nodes);
    uint64_t *node = NULL;
    size_t counter = 0;
    while (array_iter_next(&nodes_iter, (void **) &node) != CC_ITER_END) {
        size_t from_node_idx = array_iter_index(&nodes_iter);
        List *from_nodes_list = NULL;
        assert(array_get_at(graph->edges, from_node_idx, (void **) &from_nodes_list) == CC_OK);
        counter += list_size(from_nodes_list);
    }
    return counter;
}


typedef struct bfs_queue_elm {
    size_t depth;
    size_t index;
} bfs_queue_elm_t;



ssize_t graph_find_node_index(Graph *graph, uint64_t value)
{
  ArrayIter nodes_iter;
  array_iter_init(&nodes_iter, graph->nodes);
  uint64_t *node = NULL;
  while (array_iter_next(&nodes_iter, (void **) &node) != CC_ITER_END) {
    if (*node == value) {
      return array_iter_index(&nodes_iter);
    }
  }
  return -1;
}


size_t graph_depth(Graph *graph, size_t start_idx)
{
    if (graph->n_nodes == 0)
        return 0;

    assert(array_size(graph->nodes) >= start_idx);

    Queue *queue = NULL;
    assert(queue_new(&queue) == CC_OK);
    bfs_queue_elm_t *q_elm = malloc(sizeof(bfs_queue_elm_t));
    assert(q_elm != NULL);
    *q_elm = (bfs_queue_elm_t) { 0, start_idx };
    assert(queue_enqueue(queue, q_elm) == CC_OK);

    // actually the size of the nodes array should suffice
    bool discovered[graph->n_nodes];
    for (size_t i = 0; i < graph->n_nodes; i++) {
        discovered[i] = false;
    }

    size_t max_depth = q_elm->depth;
    while (queue_size(queue) > 0) {
        assert(queue_poll(queue, (void **) &q_elm) == CC_OK);
        List *edge_list = NULL;
        assert(array_get_at(graph->edges, q_elm->index, (void **) &edge_list) == CC_OK);

        ListIter edges_iter;
        list_iter_init(&edges_iter, edge_list);

        uint64_t *edge_value = NULL;
        while (list_iter_next(&edges_iter, (void **) &edge_value) != CC_ITER_END) {
            ssize_t idx = graph_find_node_index(graph, *edge_value);
            if (idx != -1 && discovered[idx] == false) {
                discovered[idx] = true;
                bfs_queue_elm_t *q_new = malloc(sizeof(bfs_queue_elm_t));
                assert(q_new != NULL);
                *q_new = (bfs_queue_elm_t) { q_elm->depth + 1, idx };
                assert(queue_enqueue(queue, q_new) == CC_OK);
            }
            if (q_elm->depth + 1 > max_depth)
                max_depth = q_elm->depth + 1;
        }

        free(q_elm);
    }

    queue_destroy(queue);

    return max_depth;
}


size_t graph_depth_conn(Graph *graph)
{
    size_t max_depth = 0;
    const size_t from_size = array_size(graph->nodes);
    for (size_t i = 0; i < from_size; i++) {
        size_t d = graph_depth(graph, i);
        if (d > max_depth)
            max_depth = d;
    }

    return max_depth;
}
