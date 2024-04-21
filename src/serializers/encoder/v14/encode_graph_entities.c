/*
 * Copyright Redis Ltd. 2018 - present
 * Licensed under your choice of the Redis Source Available License 2.0 (RSALv2) or
 * the Server Side Public License v1 (SSPLv1).
 */

#include "encode_v14.h"
#include "../../../datatypes/datatypes.h"

// forword decleration
static void _RdbSaveSIValue
(
	SerializerIO rdb,
	const SIValue *v
);

static void _RdbSaveSIArray
(
	SerializerIO rdb,
	const SIValue list
) {
	// saves array as
	// unsigned : array legnth
	// array[0]
	// .
	// .
	// .
	// array[array length -1]

	uint arrayLen = SIArray_Length(list);
	SerializerIO_WriteUnsigned(rdb, arrayLen);
	for(uint i = 0; i < arrayLen; i ++) {
		SIValue value = SIArray_Get(list, i);
		_RdbSaveSIValue(rdb, &value);
	}
}

static void _RdbSaveSIVector
(
	SerializerIO rdb, 
	SIValue v
) {
	// saves a vector
	// unsigned : vector dimension
	// vector[0]
	// .
	// .
	// .
	// vector[vector dimension -1]

	uint32_t dim = SIVector_Dim(v);
	SerializerIO_WriteUnsigned(rdb, dim);

	// get vector elements
	void *elements = SIVector_Elements(v);

	// save individual elements
	float *values = (float*)elements;
	for(uint32_t i = 0; i < dim; i ++) {
		SerializerIO_WriteFloat(rdb, values[i]);
	}
}
static void _RdBSaveMap
(
	SerializerIO rdb,
	SIValue v
) {
	// saves map as
	// unsigned : map key count
	// key:value
	// .
	// .
	// .
	// key:value

	uint n = Map_KeyCount(v);
	SerializerIO_WriteUnsigned(rdb, n);
	for(uint i = 0; i < n; i ++) {
		SIValue key;
		SIValue val;
		Map_GetIdx(v, i, &key, &val);
		_RdbSaveSIValue(rdb, &key);
		_RdbSaveSIValue(rdb, &val);
	}
}

static void _RdbSaveSIValue
(
	SerializerIO rdb,
	const SIValue *v
) {
	// Format:
	// SIType
	// Value

	SerializerIO_WriteUnsigned(rdb, v->type);

	switch(v->type) {
		case T_BOOL:
		case T_INT64:
			SerializerIO_WriteSigned(rdb, v->longval);
			break;
		case T_DOUBLE:
			SerializerIO_WriteDouble(rdb, v->doubleval);
			break;
		case T_STRING:
			SerializerIO_WriteBuffer(rdb, v->stringval,
					strlen(v->stringval) + 1);
			break;
		case T_ARRAY:
			_RdbSaveSIArray(rdb, *v);
			break;
		case T_POINT:
			SerializerIO_WriteDouble(rdb, Point_lat(*v));
			SerializerIO_WriteDouble(rdb, Point_lon(*v));
			break;
		case T_VECTOR_F32:
			_RdbSaveSIVector(rdb, *v);
			break;
		case T_MAP:
			_RdBSaveMap(rdb, *v);
			break;
		case T_NULL:
			break;  // no data beyond type needs to be encoded for NULL
		default:
			ASSERT(0 && "Attempted to serialize value of invalid type.");
	}
}

static void _RdbSaveEntity
(
	SerializerIO rdb,
	const GraphEntity *e
) {
	// Format:
	// #attributes N
	// (name, value type, value) X N 

	const AttributeSet set = GraphEntity_GetAttributes(e);
	uint16_t attr_count = AttributeSet_Count(set);

	SerializerIO_WriteUnsigned(rdb, attr_count);

	for(int i = 0; i < attr_count; i++) {
		AttributeID attr_id;
		SIValue value = AttributeSet_GetIdx(set, i, &attr_id);
		SerializerIO_WriteUnsigned(rdb, attr_id);
		_RdbSaveSIValue(rdb, &value);
	}
}

static void _RdbSaveEdge
(
	SerializerIO rdb,
	const Graph *g,
	const Edge *e,
	int r
) {

	// Format:
	//  edge ID
	//  source node ID
	//  destination node ID
	//  relation type
	//  edge properties

	SerializerIO_WriteUnsigned(rdb, ENTITY_GET_ID(e));

	// source node ID
	SerializerIO_WriteUnsigned(rdb, Edge_GetSrcNodeID(e));

	// destination node ID
	SerializerIO_WriteUnsigned(rdb, Edge_GetDestNodeID(e));

	// relation type
	SerializerIO_WriteUnsigned(rdb, r);

	// edge properties
	_RdbSaveEntity(rdb, (GraphEntity *)e);
}

static void _RdbSaveNode_v14
(
	SerializerIO rdb,
	GraphContext *gc,
	GraphEntity *n
) {
	// Format:
	//     ID
	//     #labels M
	//     (labels) X M
	//     #properties N
	//     (name, value type, value) X N */

	// save ID
	EntityID id = ENTITY_GET_ID(n);
	SerializerIO_WriteUnsigned(rdb, id);

	// retrieve node labels
	uint l_count;
	NODE_GET_LABELS(gc->g, (Node *)n, l_count);
	SerializerIO_WriteUnsigned(rdb, l_count);

	// save labels
	for(uint i = 0; i < l_count; i++) SerializerIO_WriteUnsigned(rdb, labels[i]);

	// properties N
	// (name, value type, value) X N
	_RdbSaveEntity(rdb, (GraphEntity *)n);
}

static void _RdbSaveDeletedEntities_v14
(
	SerializerIO rdb,
	GraphContext *gc,
	uint64_t deleted_entities_to_encode,
	uint64_t *deleted_id_list
) {
	// Get the number of deleted entities already encoded.
	uint64_t offset = GraphEncodeContext_GetProcessedEntitiesOffset(gc->encoding_context);

	// Iterated over the required range in the datablock deleted items.
	for(uint64_t i = offset; i < offset + deleted_entities_to_encode; i++) {
		SerializerIO_WriteUnsigned(rdb, deleted_id_list[i]);
	}
}

void RdbSaveDeletedNodes_v14
(
	SerializerIO rdb,
	GraphContext *gc,
	uint64_t deleted_nodes_to_encode
) {
	// Format:
	// node id X N

	if(deleted_nodes_to_encode == 0) return;
	// get deleted nodes list
	uint64_t *deleted_nodes_list = Serializer_Graph_GetDeletedNodesList(gc->g);
	_RdbSaveDeletedEntities_v14(rdb, gc, deleted_nodes_to_encode, deleted_nodes_list);
}

void RdbSaveDeletedEdges_v14
(
	SerializerIO rdb,
	GraphContext *gc,
	uint64_t deleted_edges_to_encode
) {
	// Format:
	// edge id X N

	if(deleted_edges_to_encode == 0) return;

	// get deleted edges list
	uint64_t *deleted_edges_list = Serializer_Graph_GetDeletedEdgesList(gc->g);
	_RdbSaveDeletedEntities_v14(rdb, gc, deleted_edges_to_encode, deleted_edges_list);
}

void RdbSaveNodes_v14
(
	SerializerIO rdb,
	GraphContext *gc,
	uint64_t nodes_to_encode
) {
	// Format:
	// Node Format * nodes_to_encode:
	//  ID
	//  #labels M
	//  (labels) X M
	//  #properties N
	//  (name, value type, value) X N

	if(nodes_to_encode == 0) return;
	// get graph's node count
	uint64_t graph_nodes = Graph_NodeCount(gc->g);
	// get the number of nodes already encoded
	uint64_t offset = GraphEncodeContext_GetProcessedEntitiesOffset(gc->encoding_context);

	// get datablock iterator from context,
	// already set to offset by a previous encodeing of nodes, or create new one
	DataBlockIterator *iter = GraphEncodeContext_GetDatablockIterator(gc->encoding_context);
	if(!iter) {
		iter = Graph_ScanNodes(gc->g);
		GraphEncodeContext_SetDatablockIterator(gc->encoding_context, iter);
	}

	for(uint64_t i = 0; i < nodes_to_encode; i++) {
		GraphEntity e;
		e.attributes = (AttributeSet *)DataBlockIterator_Next(iter, &e.id);
		_RdbSaveNode_v14(rdb, gc, &e);
	}

	// check if done encodeing nodes
	if(offset + nodes_to_encode == graph_nodes) {
		DataBlockIterator_Free(iter);
		iter = NULL;
		GraphEncodeContext_SetDatablockIterator(gc->encoding_context, iter);
	}
}

// Auxilary function to encode a multiple edges array,
// while consdirating the allowed number of edges to encode
// returns true if the number of encoded edges has reached the capacity
static void _RdbSaveMultipleEdges
(
	SerializerIO rdb,                  // RDB IO.
	GraphContext *gc,                    // Graph context.
	uint r,                              // Edges relation id.
	EdgeID *multiple_edges_array,        // Multiple edges array (passed by ref).
	uint *multiple_edges_current_index,  // Current index of the array to start encoding from (passed by ref).
	uint64_t *encoded_edges,             // Number of encoded edges in this phase (passed by ref).
	uint64_t edges_to_encode,            // Allowed capacity for encoding edges.
	NodeID src,                          // Edges source node id.
	NodeID dest                          // Edges destination node id.
) {
	uint edgeCount = array_len(multiple_edges_array);

	// define function local variables from passed-by-reference parameters.
	uint i = *multiple_edges_current_index;
	uint encoded_edges_count = *encoded_edges;

	// add edges as long the number of encoded edges is in the allowed range
	// and the array is not depleted
	while(i < edgeCount && encoded_edges_count < edges_to_encode) {
		Edge e;
		EdgeID edgeID = multiple_edges_array[i++];
		e.src_id  = src;
		e.dest_id = dest;
		Graph_GetEdge(gc->g, edgeID, &e);
		_RdbSaveEdge(rdb, gc->g, &e, r);
		encoded_edges_count++;
	}

	// update passed-by-reference parameters
	*encoded_edges = encoded_edges_count;
	*multiple_edges_current_index = i;
}

void RdbSaveEdges_v14
(
	SerializerIO rdb,
	GraphContext *gc,
	uint64_t edges_to_encode
) {
	// Format:
	// Edge format * edges_to_encode:
	//  edge ID
	//  source node ID
	//  destination node ID
	//  relation type
	//  edge properties

	GrB_Info info;
	UNUSED(info);

	if(edges_to_encode == 0) return;

	// get graph's edge count
	uint64_t graph_edges = Graph_EdgeCount(gc->g);

	// get the number of edges already encoded
	uint64_t offset = GraphEncodeContext_GetProcessedEntitiesOffset(gc->encoding_context);

	// count the edges that will be encoded in this phase
	uint64_t encoded_edges = 0;

	// get current relation matrix
	uint r = GraphEncodeContext_GetCurrentRelationID(gc->encoding_context);

	RG_Matrix M = Graph_GetRelationMatrix(gc->g, r, false);

	// get matrix tuple iterator from context
	// already set to the next entry to fetch
	// for previous edge encide or create new one
	RG_MatrixTupleIter *iter = GraphEncodeContext_GetMatrixTupleIterator(gc->encoding_context);
	if(!RG_MatrixTupleIter_is_attached(iter, M)) {
		info = RG_MatrixTupleIter_attach(iter, M);
		ASSERT(info == GrB_SUCCESS);
	}

	// first, see if the last edges encoding stopped at multiple edges array
	EdgeID *multiple_edges_array = GraphEncodeContext_GetMultipleEdgesArray(gc->encoding_context);
	NodeID src = GraphEncodeContext_GetMultipleEdgesSourceNode(gc->encoding_context);
	NodeID dest = GraphEncodeContext_GetMultipleEdgesDestinationNode(gc->encoding_context);
	uint multiple_edges_current_index = GraphEncodeContext_GetMultipleEdgesCurrentIndex(
											gc->encoding_context);
	if(multiple_edges_array) {
		_RdbSaveMultipleEdges(rdb, gc, r, multiple_edges_array,
							  &multiple_edges_current_index,
							  &encoded_edges, edges_to_encode, src, dest);
		// if the multiple edges array filled the capacity of entities allowed
		// to be encoded, finish encoding
		if(encoded_edges == edges_to_encode) {
			goto finish;
		} else {
			// reset the multiple edges context for re-use
			multiple_edges_array = NULL;
			multiple_edges_current_index = 0;
		}
	}

	uint relation_count = Graph_RelationTypeCount(gc->g);
	// write the required number of edges
	while(encoded_edges < edges_to_encode) {
		Edge e;
		EdgeID edgeID;

		// try to get next tuple
		info = RG_MatrixTupleIter_next_UINT64(iter, &src, &dest, &edgeID);

		// if iterator is depleted
		// get new tuple from different matrix or finish encode
		while(info == GxB_EXHAUSTED) {
			// proceed to next relation matrix
			r++;

			// if done iterating over all the matrices, jump to finish
			if(r == relation_count) goto finish;

			// get matrix and set iterator
			M = Graph_GetRelationMatrix(gc->g, r, false);
			info = RG_MatrixTupleIter_attach(iter, M);
			ASSERT(info == GrB_SUCCESS);
			info = RG_MatrixTupleIter_next_UINT64(iter, &src, &dest, &edgeID);
		}
		
		ASSERT(info == GrB_SUCCESS);

		e.src_id = src;
		e.dest_id = dest;
		if(SINGLE_EDGE(edgeID)) {
			Graph_GetEdge(gc->g, edgeID, &e);
			_RdbSaveEdge(rdb, gc->g, &e, r);
			encoded_edges++;
		} else {
			multiple_edges_array = (EdgeID *)(CLEAR_MSB(edgeID));
			_RdbSaveMultipleEdges(rdb, gc, r, multiple_edges_array,
								  &multiple_edges_current_index, &encoded_edges, edges_to_encode, src, dest);
			// if the multiple edges array filled the capacity of entities
			// allowed to be encoded, finish encoding
			if(encoded_edges == edges_to_encode) {
				goto finish;
			} else {
				// reset the multiple edges context for re-use
				multiple_edges_array = NULL;
				multiple_edges_current_index = 0;
			}
		}
	}

finish:
	// check if done encoding edges
	if(offset + edges_to_encode == graph_edges) {
		RG_MatrixTupleIter_detach(iter);
	}

	// update context
	GraphEncodeContext_SetCurrentRelationID(gc->encoding_context, r);
	GraphEncodeContext_SetMutipleEdgesArray(gc->encoding_context, multiple_edges_array,
											multiple_edges_current_index, src, dest);
}
