/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _das_oper_h_
#define _das_oper_h_

#include <das3/defs.h>

#ifdef __cplusplus
extern "C" {
#endif
	
/* Invalid Operator */
#define DASOP_INVALID 0
	
/* Unary operators, before */
#define DASOP_SIGN     1
#define DASOP_SQRT     7
#define DASOP_CURT     8
#define DASOP_LOG10    9
#define DASOP_LN      10
#define DASOP_COS     11
#define DASOP_SIN     12
#define DASOP_TAN     13

/* Unary operators after */
#define DASOP_SQUARE   101
#define DASOP_CUBE     102
#define DASOP_INV      103
#define DASOP_INVSQ    104
#define DASOP_INVCUBE  105
	
/* Binary operators */
#define DASOP_ADD    201
#define DASOP_SUB    202
#define DASOP_MUL    203
#define DASOP_DIV    204
#define DASOP_POW    205
	
/* Operator positions */
#define DASOP_POS_BEFORE   1
#define DASOP_POS_BETWEEN  2
#define DASOP_POS_AFTER    3


/** @addtogroup Utilities
 * @{
 */

/** Convert a string into a unary operator token
 * 
 * @param sOp a string such as "-", "sqrt", etc.
 * 
 * @return an operator token id or 0 if the string did not correspond to a 
 *         known unary operator
 */
DAS_API int dasop_unary(const char* sOp);

/** Convert a string into a binary operator token 
 *
 * @param sOp a string such as "+", "-", "*", "/", "**", "^" etc.
 * 
 * @return an operator token id or 0 if the string did not correspond to a 
 *         known unary operator
 */
DAS_API int dasop_binary(const char* sOp);

/** Provide a string representation of an operator token and an indication
 * of where the operator normally appears.
 * 
 * @param nOp The operator token value
 * 
 * @param pos a pointer to an integer to receive one of the values DASOP_POS_BEFORE, 
 *        DASOP_POS_BETWEEN or DASOP_POS_AFTER.  If pos is NULL the operator position is
 *        not set.
 * 
 * @return A pointer to a character representation of the operator or NULL if
 *         the token nOp is unknown.
 */
DAS_API const char* dasop_name(int nOp, int* pos);

/** Return true if this is a binary operation, false otherwise */
DAS_API bool dasop_isBinary(int nOp);

/** Return true if op is a unary operation, false otherwise */
DAS_API bool dasop_isUnary(int nOp);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_oper_h_ */

