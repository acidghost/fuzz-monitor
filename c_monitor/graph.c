#include "graph.h"
#include <list.h>
#include <array.h>
#include <assert.h>


struct graph_s {
    Array *nodes;       // array of nodes
    Array *edges;       // array of adjacency lists; the i-th list contains
                        // outgoing edges from i-th node in nodes
};


enum cc_stat graph_new(Graph **graph)
{
    *graph = malloc(sizeof(Graph));
    if (*graph == NULL)
        return CC_ERR_ALLOC;
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
                return CC_OK;
        }

        return list_add(from_nodes_list, to);
    }

    List *new_edge_list;
    if ((ret = list_new(&new_edge_list)) != CC_OK)
        return ret;
    if ((ret = list_add(new_edge_list, to)) != CC_OK)
        return ret;
    if ((ret = array_add(graph->nodes, from)) != CC_OK)
        return ret;
    return array_add(graph->edges, new_edge_list);
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
    return array_size(graph->nodes);
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
