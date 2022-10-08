/*
* Copyright 2018-2022 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "RG.h"
#include "index.h"
#include "../graph/rg_matrix/rg_matrix_iter.h"

#include <assert.h>

// index nodes in an asynchronous manner
// nodes are being indexed in batchs while the graph's read lock is held
// to avoid interfering with the DB ongoing operation after each batch of nodes
// is indexed the graph read lock is released
// alowing for write queries to be processed
//
// it is safe to run a write query which effects the index by either:
// adding/removing/updating an entity while the index is being populated
// in the "worst" case we will index that entity twice which is perfectly OK
static void _Index_PopulateNodeIndex
(
	Index *idx,
	Graph *g
) {
	ASSERT(g   != NULL);
	ASSERT(idx != NULL);

	GrB_Index          rowIdx     = 0;
	int                indexed    = 0;     // number of entities indexed in current batch
	int                batch_size = 1000;  // max number of entities to index in one go
	RG_MatrixTupleIter it         = {0};

	while(true) {
		// index state changed, abort indexing
		// this can happen if for example the following sequance is issued:
		// 1. CREATE INDEX FOR (n:Person) ON (n.age)
		// 2. CREATE INDEX FOR (n:Person) ON (n.height)
		if(idx->state != IDX_POPULATING) {
			break;
		}

		// reset number of indexed nodes in batch
		indexed = 0;

		// lock graph for reading
		Graph_AcquireReadLock(g);

		// fetch label matrix
		const RG_Matrix m = Graph_GetLabelMatrix(g, idx->label_id);
		ASSERT(m != NULL);

		//----------------------------------------------------------------------
		// resume scanning from rowIdx
		//----------------------------------------------------------------------

		RG_MatrixTupleIter_attach(&it, m);
		RG_MatrixTupleIter_jump_to_row(&it, rowIdx);

		//----------------------------------------------------------------------
		// batch index nodes
		//----------------------------------------------------------------------

		EntityID id;
		while(indexed < batch_size &&
			  RG_MatrixTupleIter_next_BOOL(&it, &id, NULL, NULL) == GrB_SUCCESS)
		{
			Node n;
			Graph_GetNode(g, id, &n);
			Index_IndexNode(idx, &n);
			indexed++;
		}

		// release read lock
		Graph_ReleaseLock(g);

		if(indexed != batch_size) {
			// iterator depleted, no more nodes to index
			break;
		} else {
			// finished current batch
			RG_MatrixTupleIter_detach(&it);

			// continue next batch from row id+1
			// this is true because we're iterating over a diagonal matrix
			rowIdx = id + 1;
		}
	}

	RG_MatrixTupleIter_detach(&it);
}

// index edges in an asynchronous manner
// edges are being indexed in batchs while the graph's read lock is held
// to avoid interfering with the DB ongoing operation after each batch of edges
// is indexed the graph read lock is released
// alowing for write queries to be processed
//
// it is safe to run a write query which effects the index by either:
// adding/removing/updating an entity while the index is being populated
// in the "worst" case we will index that entity twice which is perfectly OK
static void _Index_PopulateEdgeIndex
(
	Index *idx,
	Graph *g
) {
	ASSERT(g   != NULL);
	ASSERT(idx != NULL);


	GrB_Info  info;
	EntityID  src_id       = 0;     // current processed row idx
	EntityID  dest_id      = 0;     // current processed column idx
	EntityID  edge_id      = 0;     // current processed edge id
	EntityID  prev_src_id  = 0;     // last processed row idx
	EntityID  prev_dest_id = 0;     // last processed column idx
	int       indexed      = 0;     // number of entities indexed in current batch
	int       batch_size   = 1000;  // max number of entities to index in one go
	RG_MatrixTupleIter it  = {0};

	while(true) {
		// index state changed, abort indexing
		// this can happen if for example the following sequance is issued:
		// 1. CREATE INDEX FOR (:Person)-[e:WORKS]-(:Company) ON (e.since)
		// 2. CREATE INDEX FOR (:Person)-[e:WORKS]-(:Company) ON (e.title)
		if(idx->state != IDX_POPULATING) {
			break;
		}

		// reset number of indexed edges in batch
		indexed      = 0;
		prev_src_id  = src_id;
		prev_dest_id = dest_id;

		// lock graph for reading
		Graph_AcquireReadLock(g);

		// fetch relation matrix
		const RG_Matrix m = Graph_GetRelationMatrix(g, idx->label_id, false);
		ASSERT(m != NULL);

		//----------------------------------------------------------------------
		// resume scanning from previous row/col indices
		//----------------------------------------------------------------------

		RG_MatrixTupleIter_attach(&it, m);
		RG_MatrixTupleIter_jump_to_row(&it, src_id);

		// skip previously indexed edges
		while((info = RG_MatrixTupleIter_next_UINT64(&it, &src_id, &dest_id,
						NULL)) == GrB_SUCCESS &&
				src_id == prev_src_id &&
				dest_id <= prev_dest_id);

		// process only if iterator is on an active entry
		if(info != GrB_SUCCESS) {
			// release read lock
			Graph_ReleaseLock(g);
			break;
		}

		//----------------------------------------------------------------------
		// batch index edges
		//----------------------------------------------------------------------

		do {
			Edge e;
			e.srcNodeID  = src_id;
			e.destNodeID = dest_id;
			e.relationID = idx->label_id;

			if(SINGLE_EDGE(edge_id)) {
				Graph_GetEdge(g, edge_id, &e);
				Index_IndexEdge(idx, &e);
			} else {
				EdgeID *edgeIds = (EdgeID *)(CLEAR_MSB(edge_id));
				uint edgeCount = array_len(edgeIds);

				for(uint i = 0; i < edgeCount; i++) {
					edge_id = edgeIds[i];
					Graph_GetEdge(g, edge_id, &e);
					Index_IndexEdge(idx, &e);
				}
			}
			indexed++;
		} while(indexed < batch_size &&
			  RG_MatrixTupleIter_next_UINT64(&it, &src_id, &dest_id, &edge_id)
				== GrB_SUCCESS);

		// release read lock
		Graph_ReleaseLock(g);

		if(indexed != batch_size) {
			// iterator depleted, no more edges to index
			break;
		} else {
			// finished current batch
			RG_MatrixTupleIter_detach(&it);
		}
	}

	RG_MatrixTupleIter_detach(&it);
}

// constructs index
void Index_Populate
(
	Index *idx,
	Graph *g
) {
	ASSERT(g        != NULL);
	ASSERT(idx      != NULL);
	ASSERT(idx->idx != NULL);
	ASSERT(idx->state == IDX_POPULATING);

	//--------------------------------------------------------------------------
	// populate index
	//--------------------------------------------------------------------------

	if(idx->entity_type == GETYPE_NODE) {
		_Index_PopulateNodeIndex(idx, g);
	} else {
		_Index_PopulateEdgeIndex(idx, g);
	}

	// try to enable index
	Index_Enable(idx);
}
