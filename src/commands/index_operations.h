/*
 * Copyright FalkorDB Ltd. 2023 - present
 * Licensed under the Server Side Public License v1 (SSPLv1).
 */

#pragma once

#include "RG.h"
#include "../ast/ast.h"
#include "execution_ctx.h"

void IndexOperation_Run
(
	RedisModuleCtx *ctx,
	GraphContext *gc,
	AST *ast,
	ExecutionType exec_type
);