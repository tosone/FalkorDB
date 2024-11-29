/*
 * Copyright Redis Ltd. 2018 - present
 * Licensed under your choice of the Redis Source Available License 2.0 (RSALv2) or
 * the Server Side Public License v1 (SSPLv1).
 */

#pragma once

#include "../value.h"

// returns latitude of given point
float Point_lat
(
	SIValue point
);

// returns longitude of given point
float Point_lon
(
	SIValue point
);

// returns a coordinate (latitude or longitude) of a given point
SIValue Point_GetCoordinate
(
	SIValue point,
	SIValue key
);

// encode point to binary stream
void Point_ToBinary
(
	SerializerIO stream,  // binary stream
	SIValue *point        // point to encode
);

// read point from binary stream
SIValue Point_FromBinary
(
	SerializerIO stream  // binary stream
);

