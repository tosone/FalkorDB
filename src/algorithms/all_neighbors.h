/*
* Copyright 2018-2021 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#pragma once

#include "../../deps/GraphBLAS/Include/GraphBLAS.h"
#include "../graph/entities/node.h"

// performs iterative DFS from 'src'
// each iteration (call to AllNeighborsCtx_NextNeighbor)
// returns the newly discovered destination node
// it is possible for the same destination node to be returned multiple times
// if it is on multiple different paths from src
// we allow cycles to be closed, but we don't expand once a cycle been closed
// path: (a)->(b)->(a), 'a' will not be expanded again during traversal of this
// current path

typedef struct {
	EntityID src;                  // traverse begin here
	EntityID dest;                 // [optional (INVALID_ENTITY_ID)] dest node
	GrB_Matrix M;                  // adjacency matrix
	uint minLen;                   // minimum required depth
	uint maxLen;                   // maximum allowed depth
	int current_level;             // cuurent depth
	bool first_pull;               // first call to Next
	EntityID *visited;             // visited nodes
	GxB_MatrixTupleIter **levels;  // array of neighbors iterator
} AllNeighborsCtx;

AllNeighborsCtx *AllNeighborsCtx_New
(
	EntityID src,  // source node from which to traverse
	EntityID dest, // [optional (INVALID_ENTITY_ID)] destination node to reach
	GrB_Matrix M,  // matrix describing connections
	uint minLen,   // minimum traversal depth
	uint maxLen    // maximum traversal depth
);

// produce next reachable destination node
EntityID AllNeighborsCtx_NextNeighbor
(
	AllNeighborsCtx *ctx
);

void AllNeighborsCtx_Free
(
	AllNeighborsCtx *ctx
);

