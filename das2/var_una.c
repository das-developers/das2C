/* Copyright (C) 2017-2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

#define _POSIX_C_SOURCE 200112L

#include "variable.h"

/* ************************************************************************* */
/* Protected functions */ 

/* Used by the expression lexer */
DAS_API DasVar* new_DasVarUnary_tok(int nOpTok, const DasVar* pVar);


/* ************************************************************************* */
/* Unary Functions on other Variables */

typedef struct das_var_op{
	DasVar base;
	
	/* right hand sub-variable pointer for binary operations */
	const DasVar* pLeft;     
	
	/* Right hand sub-variable pointer for unary and binary operations */
	const DasVar* pRight;
	
	/* operator for unary and binary operations */
	int     nOp;
} DasVarUnary;

DasErrCode DasVarUnary_encode(DasVar* pBase, const char* sRole, DasBuf* pBuf)
{
	return das_error(DASERR_NOTIMP, "Encoding scheme for unary operations is not yet implemented.");
}


/*
DasVar* new_DasVarUnary(const char* sOp, const DasVar* left)
{
	
}



DasVar* new_DasVarUnary_tok(int nOp, const DasVar* left)
{
	/ * TODO: write this once the expression lexer exist * /
	return NULL;
}
*/