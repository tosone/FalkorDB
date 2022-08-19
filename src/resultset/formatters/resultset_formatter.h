/*
 * Copyright 2018-2022 Redis Labs Ltd. and Contributors
 *
 * This file is available under the Redis Labs Source Available License Agreement
 */

#pragma once

#include "../../redismodule.h"
#include "../../graph/graphcontext.h"
#include "../../graph/query_graph.h"

typedef enum {
	COLUMN_UNKNOWN = 0,
	COLUMN_SCALAR = 1,
	COLUMN_NODE = 2,      // Unused, retained for client compatibility.
	COLUMN_RELATION = 3,  // Unused, retained for client compatibility.
} ColumnType;

typedef enum {
	VALUE_UNKNOWN = 0,
	VALUE_NULL = 1,
	VALUE_STRING = 2,
	VALUE_INTEGER = 3,
	VALUE_BOOLEAN = 4,
	VALUE_DOUBLE = 5,
	VALUE_ARRAY = 6,
	VALUE_EDGE = 7,
	VALUE_NODE = 8,
	VALUE_PATH = 9,
	VALUE_MAP = 10,
	VALUE_POINT = 11
} ValueType;

// typedef for formatter private data creation
// this private data will be passed to both EmitHeaderFunc and EmitRowFunc
typedef void* (*FormatterPDCreateFunc)(void);

// typedef for formatter private data free
// called once the result-set had been fully encoded
typedef void (*FormatterPDFreeFunc)
(
	void *pdata
);

// typedef for header formatters
typedef void (*EmitHeaderFunc)
(
	RedisModuleCtx *ctx,   // redis module context
	const char **columns,  // result-set columns
	uint *col_rec_map,     //
	void *pdata            // formatter private data
);

// typedef for row formatters
typedef void (*EmitRowFunc)
(
	RedisModuleCtx *ctx,  // redis module context
	GraphContext *gc,     // graph context
	SIValue **row,        // row to emit
	uint numcols,         // length of row
	void *pdata           // formatter's private data
);
							   
// formatter is a collection of function pointers
typedef struct {
	EmitRowFunc            EmitRow;      // emit row
	EmitHeaderFunc         EmitHeader;   // emit header
	FormatterPDFreeFunc    FreePData;    // create formatter private data
	FormatterPDCreateFunc  CreatePData;  // free formatter private data
} ResultSetFormatter;

// redis prints doubles with up to 17 digits of precision, which captures
// the inaccuracy of many floating-point numbers (such as 0.1)
// By using the %g format and a precision of 15 significant digits, we avoid many
// awkward representations like RETURN 0.1 emitting "0.10000000000000001",
// though we're still subject to many of the typical issues with floating-point error
static inline void _ResultSet_ReplyWithRoundedDouble
(
	RedisModuleCtx *ctx,
	double d
) {
	// get length required to print number
	int len = snprintf(NULL, 0, "%.15g", d);
	char str[len + 1]; // TODO: a reusable buffer would be far preferable
	sprintf(str, "%.15g", d);
	// output string-formatted number
	RedisModule_ReplyWithStringBuffer(ctx, str, len);
}

