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

#define _POSIX_C_SOURCE 200112L

#include <string.h>

#include "util.h"
#include "operator.h"

int das_op_unary(const char* sOp){
	int nLen = strlen(sOp);
	
	switch(nLen){
	case 1:
		if(*sOp == '-') return D2UOP_SIGN;
		break;
	
	case 2:
		if((sOp[0] == 'l')&&(sOp[1] == 'n')) return D2UOP_LN;
		if((sOp[0] == '^')&&(sOp[1] == '2')) return D2UOP_SQUARE;
		if((sOp[0] == '^')&&(sOp[1] == '3')) return D2UOP_CUBE;
		if(strcmp(sOp, "²") == 0) return D2UOP_SQUARE;
		if(strcmp(sOp, "³") == 0) return D2UOP_CUBE;
		break;
	
	case 3:
		if((sOp[0] == 'l')&&(sOp[1] == 'o')&&(sOp[2] == 'g')) return D2UOP_LOG10;
		if((sOp[0] == '*')&&(sOp[1] == '*')&&(sOp[2] == '2')) return D2UOP_SQUARE;
		if((sOp[0]== '*')&&(sOp[1] == '*')&&(sOp[2] == '3')) return D2UOP_CUBE;
		if((sOp[0]== '^')&&(sOp[1] == '-')&&(sOp[2] == '1')) return D2UOP_INV;
		if((sOp[0]== '^')&&(sOp[1] == '-')&&(sOp[2] == '2')) return D2UOP_INVSQ;
		if((sOp[0]== '^')&&(sOp[1] == '-')&&(sOp[2] == '3')) return D2UOP_INVCUBE;
		if(strcmp(sOp, "√") == 0) return D2UOP_SQRT;
		if(strcmp(sOp, "∛") == 0) return D2UOP_CURT;
		break;
	
	case 4:
		if((sOp[0]=='s')&&(sOp[1]=='q')&&(sOp[2]=='r')&&(sOp[3]=='t')) return D2UOP_SQRT;
		if((sOp[0]=='c')&&(sOp[1]=='u')&&(sOp[2]=='r')&&(sOp[3]=='t')) return D2UOP_CURT;
		if((sOp[0]=='*')&&(sOp[1]=='*')&&(sOp[2]=='-')&&(sOp[3]=='1')) return D2UOP_INV;
		if((sOp[0]=='*')&&(sOp[1]=='*')&&(sOp[2]=='-')&&(sOp[3]=='2')) return D2UOP_INVSQ;
		if((sOp[0]=='*')&&(sOp[1]=='*')&&(sOp[2]=='-')&&(sOp[3]=='3')) return D2UOP_INVCUBE;
		break;
	}
	
	das_error(DASERR_OP, "Unrecognized unary operation '%s'", sOp);
	return 0;
}


int das_op_binary(const char* sOp)
{
	/* Check length 1 operators */
	int nLen = strlen(sOp);
	if(nLen == 1){
		if(*sOp == '+') return D2BOP_ADD;
		if(*sOp == '-') return D2BOP_SUB;
		if(*sOp == '*') return D2BOP_MUL;
		if(*sOp == '/') return D2BOP_DIV;
		if(*sOp == '^') return D2BOP_POW;
	}
	if(nLen == 2){
		if((sOp[0] == '*')&&(sOp[1] == '*')) return D2BOP_POW;
	}
	
	das_error(DASERR_OP, "Unrecognized binary operation '%s'", sOp);
	return 0;
}

const char* das_op_toStr(int nOp, int* pos){
	
	if(pos != NULL){
		if(nOp > 200){
			*pos = D2OP_BETWEEN;
		}
		else{
			if(nOp > 100) *pos = D2OP_AFTER;
			else *pos = D2OP_BEFORE;
		}
	}
	
	const char* p = NULL;
	switch(nOp){
	case D2UOP_SIGN: p = "-"; break;
	case D2UOP_SQRT: p = "√"; break;
	case D2UOP_CURT: p = "∛"; break;
	case D2UOP_LOG10: p = "log"; break;
	case D2UOP_LN: p = "ln"; break;
	case D2UOP_COS: p = "cos"; break;
	case D2UOP_SIN: p = "sin"; break;
	case D2UOP_TAN: p = "tan"; break;
	case D2UOP_SQUARE: p = "**2"; break;
	case D2UOP_CUBE:  p = "**3"; break;
	case D2UOP_INV: p = "**-1"; break;
	case D2UOP_INVSQ: p = "**-2"; break;
	case D2UOP_INVCUBE: p = "**-3"; break;
	case D2BOP_ADD: p = "+"; break;
	case D2BOP_SUB: p = "-"; break;
	case D2BOP_MUL: p = "*"; break;
	case D2BOP_DIV: p = "/"; break;
	case D2BOP_POW: p = "**"; break;
	}
	
	return p;
}

bool das_op_isBinary(int nOp){
	bool b = (nOp > 200 && nOp < 300);
	return b;
}

bool das_op_isUnary(int nOp){
	bool b = (nOp > 0 && nOp < 200);
	return b;
}
	
