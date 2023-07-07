/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2020 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   propagate.c
 * @brief  unit test for propagation of cons_expr
 *
 * @note: to call propagation methods we need active constraints, so we go to presolving stage
 * and create the constraint there by parsing an expression (using the transformed variables!)
 * or by explicitly constructing the tree
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/scip.h"
#include "scip/cons_expr.c"

#include "include/scip_test.h"

#define CHECK_EXPRINTERVAL(scip,expr,a,b) (SCIPisFeasEQ(scip, SCIPgetConsExprExprActivity(scip, expr).inf, (a)) && SCIPisFeasEQ(scip, SCIPgetConsExprExprActivity(scip, expr).sup, (b)))
#define EXPECTING_EXPRINTERVAL(expr,a,b) (a), (b), SCIPgetConsExprExprActivity(scip, expr).inf, SCIPgetConsExprExprActivity(scip, expr).sup

static SCIP* scip;
static SCIP_CONSHDLR* conshdlr;
static SCIP_VAR* x;
static SCIP_VAR* y;
static SCIP_VAR* z;

static
void setup(void)
{
   static SCIP_VAR* xo;
   static SCIP_VAR* yo;
   static SCIP_VAR* zo;

   SCIP_CALL( SCIPcreate(&scip) );

   /* include cons_expr: this adds the operator handlers */
   SCIP_CALL( SCIPincludeConshdlrExpr(scip) );

   /* set maxproprounds to a sufficiently large value */
   SCIP_CALL( SCIPsetIntParam(scip, "constraints/expr/maxproprounds", 1000) );

   /* accept more boundchanges without having to force them */
   SCIP_CALL( SCIPsetRealParam(scip, "numerics/boundstreps", 1e-6) );

   /* get expr conshdlr */
   conshdlr = SCIPfindConshdlr(scip, "expr");
   cr_assert_not_null(conshdlr);

   /* create problem */
   SCIP_CALL( SCIPcreateProbBasic(scip, "test_problem") );

   SCIP_CALL( SCIPcreateVarBasic(scip, &xo, "x", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &yo, "y", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &zo, "z", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPaddVar(scip, xo) );
   SCIP_CALL( SCIPaddVar(scip, yo) );
   SCIP_CALL( SCIPaddVar(scip, zo) );

   /* goto presolving */
   TESTscipSetStage(scip, SCIP_STAGE_PRESOLVING, FALSE);

   /* get transformed vars and release vars */
   SCIP_CALL( SCIPgetTransformedVar(scip, xo, &x) );
   SCIP_CALL( SCIPgetTransformedVar(scip, yo, &y) );
   SCIP_CALL( SCIPgetTransformedVar(scip, zo, &z) );
   SCIP_CALL( SCIPreleaseVar(scip, &xo) );
   SCIP_CALL( SCIPreleaseVar(scip, &yo) );
   SCIP_CALL( SCIPreleaseVar(scip, &zo) );
}

static
void teardown(void)
{
   /* free scip and check for memory leaks */
   SCIP_CALL( SCIPfree(&scip) );
   cr_assert_eq(BMSgetMemoryUsed(), 0, "There are memory leaks!");
}

static
SCIP_RETCODE propCons(
   SCIP_CONS* cons,
   SCIP_Bool* infeasible
   )
{
   SCIP_CONSEXPR_EXPR* expr;
   int ntightenings;

   assert(cons != NULL);
   assert(infeasible != NULL);

   expr = SCIPgetExprConsExpr(scip, cons);
   SCIP_CALL( forwardPropExpr(scip, conshdlr, expr, TRUE, infeasible, &ntightenings) );

   /* compare activity with constraint sides, which might add root expr to reversepropqueue */
   if( !*infeasible )
   {
      SCIP_INTERVAL conssides;

      SCIP_Real lhs = SCIPisInfinity(scip, -SCIPgetLhsConsExpr(scip, cons)) ? -SCIP_INTERVAL_INFINITY : SCIPgetLhsConsExpr(scip, cons);
      SCIP_Real rhs = SCIPisInfinity(scip,  SCIPgetRhsConsExpr(scip, cons)) ?  SCIP_INTERVAL_INFINITY : SCIPgetRhsConsExpr(scip, cons);

      SCIPintervalSetBounds(&conssides, lhs, rhs);

      SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, expr, conssides, infeasible, &ntightenings) );
   }

   SCIP_CALL( reversePropQueue(scip, conshdlr, infeasible, &ntightenings) );

   return SCIP_OKAY;
}

TestSuite(propagate, .init = setup, .fini = teardown);

Test(propagate, sum)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   SCIP_CALL( SCIPchgVarLb(scip, x, -2.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 2.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, -3.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 1.0) );

   /* create cons 0.5 <= 2x -y + 0.5 <= 1.5*/
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "2*<t_x>[C]-<t_y>[C] + 0.5", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, 0.5, 1.5) );

   /* forward prop only propagates active constraints */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );

   cr_assert_not(infeasible);
   cr_expect(SCIPisFeasEQ(scip, expr->activity.inf, -4.5), "Expecting -4.5, got %g\n",  expr->activity.inf);
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup,  7.5));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), -1.5), "Expecting -1.5, got %g\n", SCIPvarGetLbLocal(x));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), 1.0));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(y), -3.0), "Expecting -3.0, got %.20f\n", SCIPvarGetLbLocal(y));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(y), 1.0));

   /* release stuff */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, product)
{
   SCIP_CONSEXPR_EXPR* expr, *expraux;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, 1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 3.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, 2.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 4.0) );

   /* create cons 0.0 <= 0.5x^2/y <= 1 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "0.5*<t_x>^2/<t_y>", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, 0.0, 1.0) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );

   /* test stuff */
   cr_assert_not(infeasible);
   /* activity of 0.5x^2/y */
   cr_expect(SCIPisFeasEQ(scip, expr->activity.inf, 0.5*1.0 / 4.0), "Expecting %g and got %g\n", 1.0/8.0, expr->activity.inf);
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup, 0.5*3.0*3.0/2.0));

   /* activity of x^2/y */
   expraux = expr->children[0];
   cr_expect(SCIPisFeasEQ(scip, expraux->activity.inf, 1.0 / 4.0), "Expecting %g and got %g\n", 1.0/4.0, expraux->activity.inf);
   cr_expect(SCIPisFeasEQ(scip, expraux->activity.sup, 3.0*3.0/2.0));

   /* activity of x^2 */
   cr_expect(SCIPisFeasEQ(scip, expraux->children[0]->activity.inf, 1.0), "Expecting %g and got %g\n", 1.0, expraux->children[0]->activity.inf);
   cr_expect(SCIPisFeasEQ(scip, expraux->children[0]->activity.sup, 9.0), "Expecting %g and got %g\n", 9.0, expraux->children[0]->activity.sup);

   /* activity of 1/y */
   cr_expect(SCIPisFeasEQ(scip, expraux->children[1]->activity.inf, 1/4.0));
   cr_expect(SCIPisFeasEQ(scip, expraux->children[1]->activity.sup, 1/2.0));

   /* new bounds of x */
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), 1.0), "Expecting %g and got %g\n", 1.0, SCIPvarGetLbLocal(x));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), SQRT(8)));

   /* new bounds of y */
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(y), 2.0));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(y), 4.0));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, productwithzero)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y,  0.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 2.0) );
   SCIP_CALL( SCIPchgVarLb(scip, z,  0.0) ); SCIP_CALL( SCIPchgVarUb(scip, z, 3.0) );

   /* create cons 1 <= x*y*z <= 8 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "<t_x>*<t_y>*<t_z>", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_eq(expr->nchildren, 3);
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, 1.0, 8.0) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* expression image is [-6,6]
    * since consexpr relaxes bounds, we allow a large tolerance here
    */
   cr_assert_float_eq(expr->activity.inf, -6.0, SCIPfeastol(scip));
   cr_assert_float_eq(expr->activity.sup,  6.0, SCIPfeastol(scip));

   /* x*y*z >= 1 with x <= 1, y <= 2, z <= 3 should imply
    * x >= 1/6, y >= 1/3, z >= 1/2
    */
   cr_assert_float_eq(SCIPvarGetLbGlobal(x), 1.0/6.0, SCIPfeastol(scip));
   cr_assert_float_eq(SCIPvarGetLbGlobal(y), 1.0/3.0, SCIPfeastol(scip));
   cr_assert_float_eq(SCIPvarGetLbGlobal(z), 1.0/2.0, SCIPfeastol(scip));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, abs)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -3.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 4.0) );

   /* create cons 1.0 <= |x| <= 2.5 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "abs(<t_x>)", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, 1.0, 2.5) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* test that variable bounds have been tightened */
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), -2.5));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x),  2.5));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, exp)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 3.0) );

   /* create cons -1 <= exp(x) <= 2 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "exp(<t_x>[C])", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -1.0, 2.0) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* get expression and test stuff */
   cr_expect(SCIPisFeasEQ(scip, expr->activity.inf, exp(-1)));
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup, exp(3.0)));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), -1.0));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), log(2.0)));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, log)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 7.0) );

   /* create cons -1  <= log(x) <= 1 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "log(<t_x>[C])", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -1.0, 1.0) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* get expression and test stuff */
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup, log(7.0)));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), exp(-1.0)));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), exp(1.0)));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, sin)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 1.5) );

   /* create cons -1 <= sin(x) <= 0.5 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "sin(<t_x>[C])", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -1.0, 0.5) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* get expression and test stuff */
   cr_expect(SCIPisFeasEQ(scip, expr->activity.inf, SIN(-1)));
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup, SIN(1.5)));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), -1.0));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), ASIN(0.5)));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, entropy)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* originalexpr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   SCIP_Bool changed;

   /*
    * first test
    */

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, 0.3) );
   SCIP_CALL( SCIPchgVarUb(scip, x, 1.5) );

   /* create cons -1 <= entropy(x) <= 0.09482446409 */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "entropy(<t_x>[C])", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -1.0, -0.9 * log(0.9)) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* get expression and test stuff */
   cr_expect(SCIPisFeasEQ(scip, expr->activity.inf, -1.5 * log(1.5)));
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup, 1/M_E));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), 0.9));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), 1.5));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /*
    * second test
    */

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, y, -10.0) );
   SCIP_CALL( SCIPchgVarUb(scip, y, 0.2) );

   /* create cons 0.23025850929 <= entropy(y) <= 1/e */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "entropy(<t_y>[C])", NULL, &originalexpr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, originalexpr, &expr, &changed, &infeasible) );
   cr_assert_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &originalexpr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -0.1 * log(0.1), exp(-1)) );

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);

   /* get expression and test stuff */
   cr_expect(SCIPisFeasEQ(scip, expr->activity.inf, 0.0));
   cr_expect(SCIPisFeasEQ(scip, expr->activity.sup, -0.2 * log(0.2)));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(y), 0.1));
   cr_expect(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(y), 0.2));

   /* release conss */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

Test(propagate, complicated_expression)
{
   SCIP_CONSEXPR_EXPR* xexpr;
   SCIP_CONSEXPR_EXPR* yexpr;
   SCIP_CONSEXPR_EXPR* zexpr;
   SCIP_CONSEXPR_EXPR* zinvexpr;
   SCIP_CONSEXPR_EXPR* rootexpr;
   SCIP_CONSEXPR_EXPR* powexpr;
   SCIP_CONSEXPR_EXPR* sumexpr;
   SCIP_CONSEXPR_EXPR* logexpr;
   SCIP_CONSEXPR_EXPR* exprs[2];
   SCIP_Real coeffs[2];
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   int ntightenings;
   SCIP_INTERVAL conssides;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, 2.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 3.0) );
   SCIP_CALL( SCIPchgVarLb(scip, z, 1.0) ); SCIP_CALL( SCIPchgVarUb(scip, z, 2.0) );

   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &xexpr, x) );
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &yexpr, y) );
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &zexpr, z) );

   /*
    * create constraint 0 <= (-x^2 + log(y)) / z <= 2
    */

   /* log(y) */
   SCIP_CALL( SCIPcreateConsExprExprLog(scip, conshdlr, &logexpr, yexpr) );

   /* x^2 */
   coeffs[0] = 2.0;
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &powexpr, xexpr, 2.0) );

   /* log(y) - x^2 */
   coeffs[0] = 1.0;
   exprs[0] = logexpr;
   coeffs[1] = -1.0;
   exprs[1] = powexpr;
   SCIP_CALL( SCIPcreateConsExprExprSum(scip, conshdlr, &sumexpr, 2, exprs, coeffs, 0.0) );

   /* z^-1 */
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &zinvexpr, zexpr, -1.0) );

   /* (-x^2 + log(y)) / z */
   exprs[0] = sumexpr;
   exprs[1] = zinvexpr;
   SCIP_CALL( SCIPcreateConsExprExprProduct(scip, conshdlr, &rootexpr, 2, exprs, 1.0) );

   SCIPinfoMessage(scip, NULL, "test more complicated expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, rootexpr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* forward prop only propagates active constraints and active constraint live in the transformed problem */
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", rootexpr, 0.0, 2.0) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );

   cr_assert_not(infeasible);
   cr_expect(CHECK_EXPRINTERVAL(scip, xexpr, -1.0, 1.0), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(xexpr,-1.0,1.0));
   cr_expect(CHECK_EXPRINTERVAL(scip, yexpr, 2.0, 3.0), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(yexpr,2.0,3.0));
   cr_expect(CHECK_EXPRINTERVAL(scip, zexpr, 1.0, 2.0), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(zexpr,1.0,2.0));
   cr_expect(CHECK_EXPRINTERVAL(scip, logexpr, log(2), log(3)), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(logexpr,log(2),log(3)));
   cr_expect(CHECK_EXPRINTERVAL(scip, powexpr, 0.0, 1.0), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(powexpr,0.0,1.0));
   cr_expect(CHECK_EXPRINTERVAL(scip, sumexpr, log(2) - 1, log(3)), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(sumexpr,log(2)-1.0,log(3)));
   cr_expect(CHECK_EXPRINTERVAL(scip, rootexpr, (log(2) - 1) / 1.0, log(3)), "expecting [%g, %g], got [%g, %g]\n", EXPECTING_EXPRINTERVAL(rootexpr,log(2)-1.0,log(3)));

   /* initialize reverse-propagation by tightening via constraint sides */
   SCIPintervalSetBounds(&conssides, SCIPgetLhsConsExpr(scip, cons), SCIPgetRhsConsExpr(scip, cons));
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, rootexpr, conssides, &infeasible, &ntightenings) );

   cr_assert_not(infeasible);
   cr_expect(SCIPisFeasEQ(scip, rootexpr->propbounds.inf, 0.0));
   cr_expect(SCIPisFeasEQ(scip, rootexpr->propbounds.sup, log(3)));

   /* apply reverse propagation */
   SCIP_CALL( reversePropQueue(scip, conshdlr, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &rootexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &sumexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &powexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &logexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &zinvexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &zexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &yexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &xexpr) );
   SCIP_CALL( SCIPfreeTransform(scip) );
}

Test(propagate, unbounded_sub_expression)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   int ntightenings;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarUb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarUb(scip, y, 0.0) );
   SCIP_CALL( SCIPchgVarLb(scip, z, 1.0) ); SCIP_CALL( SCIPchgVarUb(scip, z, 2.0) );

   /*
    *  -5 <= 2 * x * y
    */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "2 * <t_x> * <t_y>", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -5.0, SCIPinfinity(scip)) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIPinfoMessage(scip, NULL, "test expressions with unbounded sub-expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   SCIP_CALL( propCons(cons, &infeasible) );
   cr_assert_not(infeasible);
   cr_assert(CHECK_EXPRINTERVAL(scip, expr, -SCIP_INTERVAL_INFINITY, SCIP_INTERVAL_INFINITY));

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /*
    *  -inf <= 2 + x - y <= inf
    */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "2 + <t_x> - <t_y>", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -SCIPinfinity(scip), SCIPinfinity(scip)) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIPinfoMessage(scip, NULL, "test expressions with unbounded sub-expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(CHECK_EXPRINTERVAL(scip, expr, -SCIP_INTERVAL_INFINITY, SCIP_INTERVAL_INFINITY));

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /*
    *  -inf <= x * y * z <= inf
    */
   SCIP_CALL( SCIPchgVarUb(scip, x, 0.0) );

   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "<t_x> * <t_y> * <t_z>", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -SCIPinfinity(scip), SCIPinfinity(scip)) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIPinfoMessage(scip, NULL, "test expressions with unbounded sub-expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(CHECK_EXPRINTERVAL(scip, expr, 0.0, SCIP_INTERVAL_INFINITY));

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

Test(propagate, forwardprop_uses_expressions_bounds)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   int ntightenings;
   SCIP_CONSHDLRDATA* conshdlrdata;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, 0.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, 0.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 1.0) );

   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "<t_x> + <t_y>", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -1.0, 0.5) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_expect_not(infeasible);
   cr_expect(CHECK_EXPRINTERVAL(scip, expr, 0.0, 2.0));  /* only did forward prop */

   /* change intervals of variable expressions
    * NOTE: this should not be done by a user as it interferes with the way how we recognize whether activities are uptodate
    */
   expr->children[0]->activity.inf = -1.0; expr->children[0]->activity.sup = 0.2;
   expr->children[1]->activity.inf = -1.0; expr->children[1]->activity.sup = 0.2;
   SCIPincrementConsExprCurBoundsTag(conshdlr, FALSE);
   expr->children[0]->activitytag = conshdlrdata->curboundstag;
   expr->children[1]->activitytag = conshdlrdata->curboundstag;

   /* new interval should be [0,2] intersected with [-2, 0.4]; note that it is important to have the activitytag
    * set to curboundstag; otherwise the explicitly set intervals are going to be overwritten
    */
   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_expect_not(infeasible);
   cr_expect(CHECK_EXPRINTERVAL(scip, expr, 0.0, 0.4));

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

Test(propagate, infeas_after_forwardprop)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONS* cons;
   SCIP_Bool infeasible;
   int ntightenings;
   SCIP_INTERVAL conssides;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, -1.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, 0.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 3.0) );

   /*
    * -7.0 <= 2 * x * y <= -6.1
    */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "2 * <t_x> * <t_y>", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -7.0, -6.1) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);

   SCIPintervalSetBounds(&conssides, SCIPgetLhsConsExpr(scip, cons), SCIPgetRhsConsExpr(scip, cons));
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), conssides, &infeasible, &ntightenings) );
   cr_assert(infeasible);

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /*
    * -1.0 <= 1 + x / (1 + y) <= -0.1
    */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "1 + <t_x> / (1 + <t_y>)", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -1.0, -0.1) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);

   SCIPintervalSetBounds(&conssides, SCIPgetLhsConsExpr(scip, cons), SCIPgetRhsConsExpr(scip, cons));
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), conssides, &infeasible, &ntightenings) );
   cr_assert(infeasible);

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /*
    * 0.0 <= 1 + exp(-5 * x + y^2) <= 0.9
    */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, "1 + exp(-5 * <t_x> + <t_y>^2)", NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -10.0, -0.1) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   cr_assert(SCIPconsIsActive(cons));

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);

   SCIPintervalSetBounds(&conssides, SCIPgetLhsConsExpr(scip, cons), SCIPgetRhsConsExpr(scip, cons));
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, SCIPgetExprConsExpr(scip, cons), conssides, &infeasible, &ntightenings) );
   cr_assert(infeasible);

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

Test(propagate, infeas_after_backwardprop)
{
   SCIP_CONSEXPR_EXPR* xexpr, *yexpr, *exprs[2];
   SCIP_CONSEXPR_EXPR* expr1, *expr2;
   SCIP_CONS* cons1, *cons2;
   SCIP_Bool infeasible;
   int ntightenings;
   SCIP_INTERVAL conssides;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, 0.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 2.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, 0.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 2.0) );

   /*
    * -1.0 <= x * y <= 1.0 and 3.5 <= x + y <= 5.0
    */
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &xexpr, x) );
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &yexpr, y) );
   exprs[0] = xexpr;
   exprs[1] = yexpr;

   SCIP_CALL( SCIPcreateConsExprExprProduct(scip, conshdlr, &expr1, 2, exprs, 1.0) );
   SCIP_CALL( SCIPcreateConsExprExprSum(scip, conshdlr, &expr2, 2, exprs, NULL, 0.0) );

   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons1, "cons1", expr1, -1.0, 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons1) );
   cr_assert(SCIPconsIsActive(cons1));

   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons2, "cons2", expr2, 3.5, 5.0) );
   SCIP_CALL( SCIPaddCons(scip, cons2) );
   cr_assert(SCIPconsIsActive(cons2));

   /* apply forward propagation for both constraints */
   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons1), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(CHECK_EXPRINTERVAL(scip, expr1, 0.0, 4.0));

   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons2), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(CHECK_EXPRINTERVAL(scip, expr2, 0.0, 4.0));

   /* apply constraint sides to expr2 */
   SCIPintervalSetBounds(&conssides, SCIPgetLhsConsExpr(scip, cons2), SCIPgetRhsConsExpr(scip, cons2));
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, expr2, conssides, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(SCIPisFeasEQ(scip, expr2->propbounds.inf, 3.5));
   cr_assert(SCIPisFeasEQ(scip, expr2->propbounds.sup, 4.0));

   /* reverse propagation of cons2 should lead to new bounds on x and y */
   SCIP_CALL( reversePropQueue(scip, conshdlr, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(x), 1.5));
   cr_assert(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(x), 2.0));
   cr_assert(SCIPisFeasEQ(scip, SCIPvarGetLbLocal(y), 1.5));
   cr_assert(SCIPisFeasEQ(scip, SCIPvarGetUbLocal(y), 2.0));

   /* apply forward propagation for cons1 again to update activity */
   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons1), TRUE, &infeasible, &ntightenings) );
   cr_assert_not(infeasible);
   cr_assert(CHECK_EXPRINTERVAL(scip, expr1, 1.5*1.5, 4.0));

   /* apply constraint sides to expr1 should detect infeasibility */
   SCIPintervalSetBounds(&conssides, SCIPgetLhsConsExpr(scip, cons1), SCIPgetRhsConsExpr(scip, cons1));
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, expr1, conssides, &infeasible, &ntightenings) );
   cr_assert(infeasible);

   SCIP_CALL( SCIPreleaseCons(scip, &cons2) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons1) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr2) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr1) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &yexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &xexpr) );
}

/** tests whether forward propagation handles common subexpressions correctly
 *
 * the test creates x^2 >= 0.5 and x^2 * y <= 1 with x in [0,1] and y in [1,3] and checks whether the
 * infimum of x^2 * y is 0.5
 */
Test(propagate, forwardprop_common_subexpressions)
{
   SCIP_CONSEXPR_EXPR* xexpr;
   SCIP_CONSEXPR_EXPR* yexpr;
   SCIP_CONSEXPR_EXPR* sqrexpr;
   SCIP_CONSEXPR_EXPR* prodexpr;
   SCIP_CONS* cons1;
   SCIP_CONS* cons2;
   SCIP_INTERVAL interval;
   SCIP_Bool infeasible;
   int ntightenings;

   /* change bounds of vars */
   SCIP_CALL( SCIPchgVarLb(scip, x, 0.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, 1.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 3.0) );

   /* create expressions */
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &xexpr, x) );
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &yexpr, y) );
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &sqrexpr, xexpr, 2.0) );
   SCIP_CALL( SCIPcreateConsExprExprProduct(scip, conshdlr, &prodexpr, 0, NULL, 1.0) );
   SCIP_CALL( SCIPappendConsExprExprProductExpr(scip, prodexpr, sqrexpr) );
   SCIP_CALL( SCIPappendConsExprExprProductExpr(scip, prodexpr, yexpr) );

   /* create constraints */
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons1, "cons1", sqrexpr, 0.5, SCIPinfinity(scip)) );
   SCIP_CALL( SCIPaddCons(scip, cons1) );
   cr_assert(SCIPconsIsActive(cons1));
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons2, "cons2", prodexpr, -SCIPinfinity(scip), 1.0) );
   SCIP_CALL( SCIPaddCons(scip, cons2) );
   cr_assert(SCIPconsIsActive(cons2));

   /* apply propagation for first constraint (x^2 >= 0.5) */
   SCIP_CALL( propCons(cons1, &infeasible) );

   cr_expect_not(infeasible);
   cr_expect_float_eq(SCIPvarGetLbLocal(x), sqrt(0.5), 1e-6, "expected: sqrt(0.5) got: %g\n", SCIPvarGetLbLocal(x));

   /* apply forward propagation for root expression of 2nd constraint (x^2 * y) */
   SCIP_CALL( forwardPropExpr(scip, conshdlr, SCIPgetExprConsExpr(scip, cons2), TRUE, &infeasible, &ntightenings) );

   interval = SCIPgetConsExprExprActivity(scip, sqrexpr);
   cr_expect_float_eq(SCIPintervalGetInf(interval), 0.5, 1e-6, "expected: 0.5 got: %g\n", SCIPintervalGetInf(interval));
   cr_expect_float_eq(SCIPintervalGetSup(interval), 1.0, 1e-6, "expected: 1.0 got: %g\n", SCIPintervalGetSup(interval));

   cr_expect_not(infeasible);
   interval = SCIPgetConsExprExprActivity(scip, prodexpr);
   cr_expect_float_eq(SCIPintervalGetInf(interval), 0.5, 1e-6, "expected: 0.5 got: %g\n", SCIPintervalGetInf(interval));
   cr_expect_float_eq(SCIPintervalGetSup(interval), 3.0, 1e-6, "expected: 3.0 got: %g\n", SCIPintervalGetSup(interval));

   /* release memory */
   SCIP_CALL( SCIPreleaseCons(scip, &cons2) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons1) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &prodexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &sqrexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &yexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &xexpr) );
}

struct expr_results
{
   const char cons1[1000];
   const char cons2[1000];
   SCIP_Real xlb;
   SCIP_Real xub;
};

ParameterizedTestParameters(propagate, propConss)
{
   static const struct expr_results data[] =
   {
      {"<t_x>^2 + <t_x>", "<t_x>^2 - 1.0", -1.0, -1.0},
      {"<t_x>^(0.5) - <t_y>","<t_x> - 1.0 - <t_y>", 2.618033988749895, 2.618033988749895},
      {"exp(<t_x>) - <t_y>", "4.0 * <t_x>^(1.5) - <t_y>", 0.58687228932071, 3.06767359040726},
      {"log(abs(<t_x> + 1)) - <t_y>", "abs(<t_x>)^1.5 - <t_y>", 0.0,  0.6096527513}
   };

   return cr_make_param_array(const struct expr_results, data, sizeof(data)/sizeof(struct expr_results));
}

ParameterizedTest(const struct expr_results* data, propagate, propConss)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONS* cons1, *cons2;
   int nchgbds = 0;
   SCIP_RESULT result;

   /* set variable bounds */
   SCIP_CALL( SCIPchgVarLb(scip, x, -10.0) ); SCIP_CALL( SCIPchgVarUb(scip, x, 10.0) );
   SCIP_CALL( SCIPchgVarLb(scip, y, -100.0) ); SCIP_CALL( SCIPchgVarUb(scip, y, 100.0) );
   SCIP_CALL( SCIPchgVarLb(scip, z, -3.0) ); SCIP_CALL( SCIPchgVarUb(scip, z, 1.0) );

   SCIPinfoMessage(scip, NULL, "test constraints: %s == 0 and %s == 0 \n", data->cons1, data->cons2);

   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, data->cons1, NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons1, "cons1", expr, 0.0, 0.0) );
   SCIP_CALL( SCIPaddCons(scip, cons1) );
   cr_assert(SCIPconsIsActive(cons1));
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, data->cons2, NULL, &expr) );
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons2, "cons2", expr, 0.0, 0.0) );
   SCIP_CALL( SCIPaddCons(scip, cons2) );
   cr_assert(SCIPconsIsActive(cons1));
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /* call propConss() for all transformed constraints */
   cr_assert_not_null(SCIPconshdlrGetConss(conshdlr));
   cr_assert_eq(SCIPconshdlrGetNConss(conshdlr), 2);
   SCIP_CALL( propConss(scip, conshdlr, SCIPconshdlrGetConss(conshdlr), SCIPconshdlrGetNConss(conshdlr), TRUE, &result, &nchgbds) );
   cr_assert_eq(result, SCIP_REDUCEDDOM, "expecting %d, but got %d\n", SCIP_REDUCEDDOM, result);
   cr_assert_gt(nchgbds, 0);

   /* check bounds */
   cr_expect(SCIPvarGetLbLocal(x) <= data->xlb);
   cr_expect(SCIPvarGetUbLocal(x) >= data->xub);
   /* @benny: the tolerance is pretty bad for some tests, can you have a look please? */
   cr_expect(REALABS(SCIPvarGetLbLocal(x) - data->xlb) <= 1e-4, "Expecting %g, got %g\n", data->xlb, SCIPvarGetLbLocal(x));
   cr_expect(REALABS(SCIPvarGetUbLocal(x) - data->xub) <= 1e-4, "Expecting %g, got %g\n", data->xub, SCIPvarGetUbLocal(x));

   /* free transformed problem and remove constraints */
   SCIP_CALL( SCIPdelCons(scip, cons1) );
   SCIP_CALL( SCIPdelCons(scip, cons2) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons1) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons2) );
}
