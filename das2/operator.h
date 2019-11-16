/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 *
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _das_operator_h_
#define _das_operator_h_

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup datasets 
 * @{
 */
	
/* Invalid Operator */
#define D2OP_INVALID 0
	
/* Unary operators, before */
#define D2UOP_SIGN     1
#define D2UOP_SQRT     7
#define D2UOP_CURT     8
#define D2UOP_LOG10    9
#define D2UOP_LN      10
#define D2UOP_COS     11
#define D2UOP_SIN     12
#define D2UOP_TAN     13

/* Unary operators after */
#define D2UOP_SQUARE   101
#define D2UOP_CUBE     102
#define D2UOP_INV      103
#define D2UOP_INVSQ    104
#define D2UOP_INVCUBE  105
	
/* Binary operators */
#define D2BOP_ADD    201
#define D2BOP_SUB    202
#define D2BOP_MUL    203
#define D2BOP_DIV    204
#define D2BOP_POW    205
	
/* Operator positions */
#define D2OP_BEFORE   1
#define D2OP_BETWEEN  2
#define D2OP_AFTER    3

/** Convert a string into a unary operator token
 * 
 * @param sOp a string such as "-", "sqrt", etc.
 * 
 * @return an operator token id or 0 if the string did not corespond to a 
 *         known unary operator
 */
DAS_API int das_op_unary(const char* sOp);

/** Convert a string into a binary operator token 
 *
 * @param sOp a string such as "+", "-", "*", "/", "**", "^" etc.
 * 
 * @return an operator token id or 0 if the string did not corespond to a 
 *         known unary operator
 */
DAS_API int das_op_binary(const char* sOp);

/** Provide a string representation of an operator token and an indication
 * of where the operator normally appears.
 * 
 * @param nOp The operator token value
 * 
 * @param pos a pointer to an integer to receive one of the values D2OP_BEFORE, 
 *        D2OP_BETWEEN or D2OP_AFTER.  If pos is NULL the operator position is
 *        not set.
 * 
 * @return A pointer to a character representation of the operator or NULL if
 *         the token nOp is unknown.
 */
DAS_API const char* das_op_toStr(int nOp, int* pos);

/** Return true if this is a binary operation, false otherwise */
DAS_API bool das_op_isBinary(int nOp);

/** Return true if op is a unary operation, false otherwise */
DAS_API bool das_op_isUnary(int nOp);

/** @} */


#ifdef __cplusplus
}
#endif

#endif /* _das_operator_h_ */

