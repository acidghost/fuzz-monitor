#ifndef _H_GRAPH_
#define _H_GRAPH_

#include <inttypes.h>
#include <unistd.h>


// beware of this...
#define CC_GRAPH_FROM_EXISTS        CC_ERR_KEY_NOT_FOUND
#define CC_GRAPH_BOTH_EXIST         CC_ERR_VALUE_NOT_FOUND


typedef struct graph_s Graph;

enum cc_stat graph_new(Graph **graph);
void         graph_destroy(Graph *graph);
enum cc_stat graph_add(Graph *graph, uint64_t *from, uint64_t *to);
void         graph_foreach(Graph *graph, void *data, void (*fn)(uint64_t *, uint64_t **, size_t, void*));
size_t       graph_nodes(Graph *graph);
size_t       graph_edges(Graph *graph);
size_t       graph_depth(Graph *graph, size_t start_idx);
size_t       graph_depth_conn(Graph *graph);

#endif
