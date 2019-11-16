  /* Use Flex's support for input line numbers */
%option yylineno
%option prefix="dataset_psr_"

%{
#include <stdio.h>

%}

op_ptrn \*\*|[\+\-\*\/\^~]
func_ptrn [a-zA-Z]+
ary_name_ptrn \$[a-zA-Z_][a-zA-Z0-9_\-]*
ds_idx_ptrn \[[i-xI-X]\]
const_idx_ptrn \[[0-9]+\]
int_ptrn   (-?[0-9]+)|(0x[0-9a-fA-F]+)
real_ptrn   -?((([0-9]+)|([0-9]*\.[0-9]+))([eE][-+]?[0-9]+)?)


%%

{op_ptrn}       { printf("   Operator: '%s'\n", yytext);}
{func_ptrn}     { printf("   Function: %s\n", yytext); }
{ary_name_ptrn} { printf("   Array: %s\n", yytext+1); }
{ds_idx_ptrn}   { printf("   Dataset Index: %s\n", yytext); }
{const_idx_ptrn} { printf("   Constant Index: %s\n", yytext); }
{int_ptrn}       { printf("   Integer: %s\n", yytext); }
{real_ptrn}      { printf("   Real:    %s\n", yytext); }
\(              { printf("   Start Group\n");}
\)              { printf("   End Group\n");}
,               { printf("   Func Arg Sep\n");}
[[:space:]]  { /* Do nothing with spaces, newlines tabs or carrage returns */ }

. { 
   /* Send all other characters out as return values */ 
   printf("Unknown character '%c'\n", yytext[0]);
}

%%

int yywrap(void){return 1;}  /* We don't have multiple parsers to write */

int main(int argc, char **argv)
{
  yylex();
  printf("Done\n");
  return 0;
}
