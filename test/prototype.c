#include <stdio.h>

double cat(long a, long b){
	printf(" sizeof a = %zu, val a = %ld\n", sizeof(a), a);
	printf(" sizeof b = %zu, val b = %ld\n", sizeof(b), b);
	return 1.0;
}


double cat(int nSize, long* pVals);


long loc[4] = {1, 2, 2, 4};
cat(4,  )
