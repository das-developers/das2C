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

#define _POSIX_C_SOURCE 200112L

#include <string.h>

#include "util.h"
#include "oper.h"

int dasop_unary(const char* sOp){
	int nLen = strlen(sOp);
	
	switch(nLen){
	case 1:
		if(*sOp == '-') return DASOP_SIGN;
		break;
	
	case 2:
		if((sOp[0] == 'l')&&(sOp[1] == 'n')) return DASOP_LN;
		if((sOp[0] == '^')&&(sOp[1] == '2')) return DASOP_SQUARE;
		if((sOp[0] == '^')&&(sOp[1] == '3')) return DASOP_CUBE;
		if(strcmp(sOp, "²") == 0) return DASOP_SQUARE;
		if(strcmp(sOp, "³") == 0) return DASOP_CUBE;
		break;
	
	case 3:
		if((sOp[0] == 'l')&&(sOp[1] == 'o')&&(sOp[2] == 'g')) return DASOP_LOG10;
		if((sOp[0] == '*')&&(sOp[1] == '*')&&(sOp[2] == '2')) return DASOP_SQUARE;
		if((sOp[0]== '*')&&(sOp[1] == '*')&&(sOp[2] == '3')) return DASOP_CUBE;
		if((sOp[0]== '^')&&(sOp[1] == '-')&&(sOp[2] == '1')) return DASOP_INV;
		if((sOp[0]== '^')&&(sOp[1] == '-')&&(sOp[2] == '2')) return DASOP_INVSQ;
		if((sOp[0]== '^')&&(sOp[1] == '-')&&(sOp[2] == '3')) return DASOP_INVCUBE;
		if(strcmp(sOp, "√") == 0) return DASOP_SQRT;
		if(strcmp(sOp, "∛") == 0) return DASOP_CURT;
		break;
	
	case 4:
		if((sOp[0]=='s')&&(sOp[1]=='q')&&(sOp[2]=='r')&&(sOp[3]=='t')) return DASOP_SQRT;
		if((sOp[0]=='c')&&(sOp[1]=='u')&&(sOp[2]=='r')&&(sOp[3]=='t')) return DASOP_CURT;
		if((sOp[0]=='*')&&(sOp[1]=='*')&&(sOp[2]=='-')&&(sOp[3]=='1')) return DASOP_INV;
		if((sOp[0]=='*')&&(sOp[1]=='*')&&(sOp[2]=='-')&&(sOp[3]=='2')) return DASOP_INVSQ;
		if((sOp[0]=='*')&&(sOp[1]=='*')&&(sOp[2]=='-')&&(sOp[3]=='3')) return DASOP_INVCUBE;
		break;
	}
	
	daserrf(DASERR_OP, "Unrecognized unary operation '%s'", sOp);
	return 0;
}


int dasop_binary(const char* sOp)
{
	/* Check length 1 operators */
	int nLen = strlen(sOp);
	if(nLen == 1){
		if(*sOp == '+') return DASOP_ADD;
		if(*sOp == '-') return DASOP_SUB;
		if(*sOp == '*') return DASOP_MUL;
		if(*sOp == '/') return DASOP_DIV;
		if(*sOp == '^') return DASOP_POW;
	}
	if(nLen == 2){
		if((sOp[0] == '*')&&(sOp[1] == '*')) return DASOP_POW;
	}
	
	daserrf(DASERR_OP, "Unrecognized binary operation '%s'", sOp);
	return 0;
}

const char* dasop_name(int nOp, int* pos){
	
	if(pos != NULL){
		if(nOp > 200){
			*pos = DASOP_POS_BETWEEN;
		}
		else{
			if(nOp > 100) *pos = DASOP_POS_AFTER;
			else *pos = DASOP_POS_BEFORE;
		}
	}
	
	const char* p = NULL;
	switch(nOp){
	case DASOP_SIGN: p = "-"; break;
	case DASOP_SQRT: p = "√"; break;
	case DASOP_CURT: p = "∛"; break;
	case DASOP_LOG10: p = "log"; break;
	case DASOP_LN: p = "ln"; break;
	case DASOP_COS: p = "cos"; break;
	case DASOP_SIN: p = "sin"; break;
	case DASOP_TAN: p = "tan"; break;
	case DASOP_SQUARE: p = "**2"; break;
	case DASOP_CUBE:  p = "**3"; break;
	case DASOP_INV: p = "**-1"; break;
	case DASOP_INVSQ: p = "**-2"; break;
	case DASOP_INVCUBE: p = "**-3"; break;
	case DASOP_ADD: p = "+"; break;
	case DASOP_SUB: p = "-"; break;
	case DASOP_MUL: p = "*"; break;
	case DASOP_DIV: p = "/"; break;
	case DASOP_POW: p = "**"; break;
	}
	
	return p;
}

bool dasop_isBinary(int nOp){
	bool b = (nOp > 200 && nOp < 300);
	return b;
}

bool dasop_isUnary(int nOp){
	bool b = (nOp > 0 && nOp < 200);
	return b;
}
	
