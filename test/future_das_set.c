/* Unit tests for the DasSet / DasGen redesign (das2/set.h, das2/generator.h).
 *
 * Grows case by case as the layer grows; the manifest of intended coverage is
 * at bottom.  Keep it C99 clean: no _Static_assert, use runtime checks.
 *
 * Design record: co_notes/libdas_wire_model_pilot.md,
 * co_notes/libdas_type_extension_map.md, co_notes/libdas_set_sketch_notes.md.
 */

#include <stdio.h>
#include <string.h>

#include <das2/core.h>
#include <das2/set.h>

#define CHECK(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		return 13; \
	}

/* Case 1: et is a pinned subset of vt; assert they have not drifted. */
static int test_elem_drift(void)
{
	CHECK(etUnknown == (int)vtUnknown);
	CHECK(etUByte   == (int)vtUByte);
	CHECK(etByte    == (int)vtByte);
	CHECK(etUShort  == (int)vtUShort);
	CHECK(etShort   == (int)vtShort);
	CHECK(etUInt    == (int)vtUInt);
	CHECK(etInt     == (int)vtInt);
	CHECK(etULong   == (int)vtULong);
	CHECK(etLong    == (int)vtLong);
	CHECK(etFloat   == (int)vtFloat);
	CHECK(etDouble  == (int)vtDouble);
	CHECK(etTime    == (int)vtTime);
	return 0;
}

/* Case 2: generator eval for constant and sequence sources */
static int test_gen_const_seq(void)
{
	ptrdiff_t aShape[1] = { DASIDX_RAGGED };
	double rVal = 4.75;
	DasGen* pConst = new_DasGenConst(etDouble, (const ubyte*)&rVal, 1, aShape);
	CHECK(pConst != NULL);

	ptrdiff_t aLoc[1] = { 117 };
	double rOut = 0.0;
	CHECK(DasGen_eval(pConst, aLoc, (ubyte*)&rOut, sizeof(rOut)) == 1);
	CHECK(rOut == 4.75);

	/* a 100 Hz offset sequence: 0.01 s per sample */
	double rIntercept = 0.0, rSlope = 0.01;
	DasGen* pSeq = new_DasGenSeq(
		etDouble, (const ubyte*)&rIntercept, 1, (const ubyte*)&rSlope, aShape
	);
	CHECK(pSeq != NULL);
	aLoc[0] = 250;
	CHECK(DasGen_eval(pSeq, aLoc, (ubyte*)&rOut, sizeof(rOut)) == 1);
	CHECK(rOut == 2.5);

	/* a TT2000 tick sequence, whole nanoseconds */
	int64_t nIntercept = 1000000000LL, nSlope = 250000LL;
	DasGen* pTicks = new_DasGenSeq(
		etLong, (const ubyte*)&nIntercept, 1, (const ubyte*)&nSlope, aShape
	);
	CHECK(pTicks != NULL);
	aLoc[0] = 4;
	int64_t nOut = 0;
	CHECK(DasGen_eval(pTicks, aLoc, (ubyte*)&nOut, sizeof(nOut)) == 1);
	CHECK(nOut == 1001000000LL);

	CHECK(DasGen_decRef(pConst) == 0);
	CHECK(DasGen_decRef(pSeq) == 0);
	CHECK(DasGen_decRef(pTicks) == 0);
	return 0;
}

/* Case 2b: generator eval over a backing array, including an internal run */
static int test_gen_array(void)
{
	float fill = -1.0f;
	DasAry* pAry = new_DasAry(
		"b_vals", vtFloat, 0, (const ubyte*)&fill, RANK_2(0, 3), UNIT_DIMENSIONLESS
	);
	CHECK(pAry != NULL);

	float aVals[12];
	for(int i = 0; i < 12; ++i) aVals[i] = (float)i;
	CHECK(DasAry_append(pAry, (const ubyte*)aVals, 12) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	CHECK(pGen != NULL);
	CHECK(DasGen_elemType(pGen) == etFloat);

	/* records of 3: the item run at external index 2 is {6,7,8} */
	ptrdiff_t aLoc[1] = { 2 };
	float aRun[3] = {0.0f, 0.0f, 0.0f};
	CHECK(DasGen_eval(pGen, aLoc, (ubyte*)aRun, sizeof(aRun)) == 3);
	CHECK((aRun[0] == 6.0f)&&(aRun[1] == 7.0f)&&(aRun[2] == 8.0f));

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasGen_extShape(pGen, aShape) == 1);
	CHECK(aShape[0] == 4);

	CHECK(DasGen_decRef(pGen) == 0);
	dec_DasAry(pAry);
	return 0;
}

/* Case 4: formalism table lookups, hit and miss */
static int test_form_table(void)
{
	const das_form_kind* pPoint = das_form_lookup("point");
	CHECK(pPoint != NULL);
	CHECK(strcmp(pPoint->sToken, "point") == 0);

	CHECK(das_form_lookup("geovec") == NULL);   /* no row yet: generic */
	CHECK(das_form_lookup("") == NULL);
	CHECK(das_form_lookup(NULL) == NULL);

	das_formalism form;
	CHECK(das_formalism_init(&form, "geovec") == DAS_OKAY);
	CHECK(form.pKind == NULL);                       /* miss is generic */
	CHECK(strcmp(form.sToken, "geovec") == 0);       /* token kept       */

	CHECK(das_formalism_init(&form, "point") == DAS_OKAY);
	CHECK(form.pKind == pPoint);

	/* no <formalism> binds the EXPLICIT linear row; NULL is reserved for
	   the generic (rules unknown) case alone */
	CHECK(das_formalism_init(&form, NULL) == DAS_OKAY);
	CHECK(form.pKind == das_form_linear());
	CHECK(form.sToken[0] == '\0');
	return 0;
}

/* The linear ring: plain numbers' common operators, registered like any
   other rules so nothing bypasses the registry */
static int test_linear_rules(void)
{
	const das_form_kind* pLin = das_form_linear();
	das_formalism formLin, formOut;
	das_formalism_init(&formLin, NULL);

	const das_form_rule* pRule = das_form_findRule(dfoAdd, pLin, pLin);
	CHECK(pRule != NULL);
	das_units uOut = NULL;
	CHECK(pRule->resolve(&formLin, &formLin, UNIT_HERTZ, UNIT_HERTZ,
	                     &formOut, &uOut));
	CHECK(uOut == UNIT_HERTZ);
	CHECK(formOut.pKind == pLin);

	/* mixed units refuse rather than guess */
	CHECK(!pRule->resolve(&formLin, &formLin, UNIT_HERTZ, UNIT_SECONDS,
	                      &formOut, &uOut));

	CHECK(das_form_findRule(dfoSub, pLin, pLin) != NULL);

	pRule = das_form_findRule(dfoMul, pLin, pLin);
	CHECK(pRule != NULL);
	CHECK(pRule->resolve(&formLin, &formLin, UNIT_SECONDS, UNIT_HERTZ,
	                     &formOut, &uOut));
	double rL = 4.0, rR = 2.5, rOut = 0.0;
	CHECK(pRule->apply(etDouble, (const ubyte*)&rL, (const ubyte*)&rR,
	                   (ubyte*)&rOut));
	CHECK(rOut == 10.0);

	/* a GENERIC formalism (unknown token, pKind NULL) matches no slot:
	   unknown math is refused by construction, not by special case */
	CHECK(das_form_findRule(dfoAdd, NULL, NULL) == NULL);
	CHECK(das_form_findRule(dfoAdd, NULL, pLin) == NULL);
	CHECK(das_form_findRule(dfoMul, pLin, NULL) == NULL);
	return 0;
}

/* Case 7: point algebra through the binop registry */
static int test_point_rules(void)
{
	const das_form_kind* pPoint = das_form_lookup("point");
	const das_form_kind* pLin = das_form_linear();
	das_formalism formPoint, formLinear, formOut;
	das_formalism_init(&formPoint, "point");
	das_formalism_init(&formLinear, NULL);

	/* point - point = interval */
	const das_form_rule* pRule = das_form_findRule(dfoSub, pPoint, pPoint);
	CHECK(pRule != NULL);

	das_units uOut = NULL;
	CHECK(pRule->resolve(&formPoint, &formPoint, UNIT_US2000, UNIT_US2000,
	                     &formOut, &uOut));
	CHECK(uOut == UNIT_MICROSECONDS);
	CHECK(formOut.pKind == pLin);               /* an interval is linear */

	int64_t nL = 5000000LL, nR = 3000000LL, nOut = 0;
	CHECK(pRule->apply(etLong, (const ubyte*)&nL, (const ubyte*)&nR,
	                   (ubyte*)&nOut));
	CHECK(nOut == 2000000LL);

	/* mismatched epochs refuse */
	CHECK(!pRule->resolve(&formPoint, &formPoint, UNIT_US2000, UNIT_T1970,
	                      &formOut, &uOut));

	/* point + interval = point, both argument orders, each stated */
	pRule = das_form_findRule(dfoAdd, pPoint, pLin);
	CHECK(pRule != NULL);
	CHECK(pRule->resolve(&formPoint, &formLinear, UNIT_US2000,
	                     UNIT_MICROSECONDS, &formOut, &uOut));
	CHECK(uOut == UNIT_US2000);
	CHECK(formOut.pKind == pPoint);

	/* wrong interval units refuse */
	CHECK(!pRule->resolve(&formPoint, &formLinear, UNIT_US2000, UNIT_SECONDS,
	                      &formOut, &uOut));

	CHECK(das_form_findRule(dfoAdd, pLin, pPoint) != NULL);

	/* point + point is a lookup MISS, the refusal is the absence */
	CHECK(das_form_findRule(dfoAdd, pPoint, pPoint) == NULL);
	CHECK(das_form_findRule(dfoMul, pPoint, pPoint) == NULL);
	return 0;
}

/* Case 3 (scalar slice): a set reads back the right datum */
static int test_scalar_set(void)
{
	ptrdiff_t aShape[1] = { DASIDX_RAGGED };
	int64_t nIntercept = 0LL, nSlope = 7812500LL;   /* 128 Hz in TT2000 ns */
	DasGen* pGen = new_DasGenSeq(
		etLong, (const ubyte*)&nIntercept, 1, (const ubyte*)&nSlope, aShape
	);
	CHECK(pGen != NULL);

	DasSet* pTime = new_DasSetScalar(pGen, UNIT_TT2000, "point");
	CHECK(pTime != NULL);
	CHECK(DasSet_family(pTime) == sfScalar);
	CHECK(DasSet_presType(pTime) == prScalar);   /* derived, not stored */
	CHECK(pTime->form.pKind == das_form_lookup("point"));

	ptrdiff_t aLoc[1] = { 2 };
	das_datum dm;
	CHECK(DasSet_get(pTime, aLoc, &dm));
	CHECK(dm.vt == vtLong);
	CHECK(dm.units == UNIT_TT2000);
	CHECK(*((int64_t*)&dm) == 15625000LL);

	ptrdiff_t aSetShape[DASIDX_MAX];
	CHECK(DasSet_shape(pTime, aSetShape) == 1);
	CHECK(aSetShape[0] == DASIDX_RAGGED);

	/* the generator survives the set: two owners, then one, then zero */
	CHECK(DasGen_incRef(pGen) == 3);   /* mine + the set's + this probe */
	CHECK(DasGen_decRef(pGen) == 2);
	CHECK(DasSet_decRef(pTime) == 0);
	CHECK(DasGen_decRef(pGen) == 0);
	return 0;
}

/* Units ruling: a ';' units list fails loud rather than misstating data */
static int test_units_list_refused(void)
{
	ptrdiff_t aShape[1] = { DASIDX_RAGGED };
	double rVal = 1.0;
	DasGen* pGen = new_DasGenConst(etDouble, (const ubyte*)&rVal, 1, aShape);
	CHECK(pGen != NULL);

	/* the interning layer (Units_fromStr) already refuses a ';' list, so probe
	   the set-level guard directly with a raw string */
	DasSet* pSet = new_DasSetScalar(
		pGen, (das_units)"degrees;degrees;km", NULL
	);
	CHECK(pSet == NULL);

	CHECK(DasGen_decRef(pGen) == 0);
	return 0;
}

int main(int argc, char** argv)
{
	(void)argc;
	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	int nRet = 0;
	if((nRet = test_elem_drift()) != 0)         return nRet;
	if((nRet = test_gen_const_seq()) != 0)      return nRet;
	if((nRet = test_gen_array()) != 0)          return nRet;
	if((nRet = test_form_table()) != 0)         return nRet;
	if((nRet = test_linear_rules()) != 0)       return nRet;
	if((nRet = test_point_rules()) != 0)        return nRet;
	if((nRet = test_scalar_set()) != 0)         return nRet;
	if((nRet = test_units_list_refused()) != 0) return nRet;

	printf("INFO: future_das_set: all DasSet/DasGen layer checks passed\n");
	return 0;
}

/* Coverage manifest, grown case by case:
 *
 * DONE 1. Element vs value enum drift (runtime, C99 safe).
 * DONE 2. Generator eval: constant, sequence (double + TT2000 long), array
 *         with an internal item run.  gtBinop/gtUnop arrive with the
 *         operator migration; scalar-only, fail loud on composites.
 * PART 3. Presentation round trip: scalar covered; string, blob, vector,
 *         complex, rotation, matrix, generic arrive with DasCompSet.
 * DONE 4. Formalism table: hit, miss-is-generic, token kept on miss.
 * TODO 5. Byte run branch: string sentinel vs blob ptr+len.
 * TODO 6. Structural metadata: per component labels; units lists currently
 *         REFUSED by ruling (test_units_list_refused).
 * DONE 7. Point algebra via the registry: p-p=i, p+i=p (both orders stated),
 *         p+p refused by lookup miss, epoch and interval unit mismatches
 *         refused by resolve.
 * TODO 8. Wire counts (numItems vs intern vs itemBytes) once the reader
 *         speaks the new elements.
 */
