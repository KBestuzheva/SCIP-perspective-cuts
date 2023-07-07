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

/**@file   nlhdlr_quadratic.c
 * @brief  tests quadratic nonlinear handler methods
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <string.h>

/* XXX: need the consdata struct because we don't have getNlhdlrs or findNlhdlrs; I don't add those function because I'm unsure
 * we actually need them
 */
#define SCIP_PRIVATE_ROWPREP
#include "scip/cons_expr.c"
#include "scip/cons_expr_nlhdlr_quadratic.c"


/*
 * TEST
 */

#include "include/scip_test.h"

static SCIP* scip;
static SCIP_VAR* x;
static SCIP_VAR* y;
static SCIP_VAR* w;
static SCIP_VAR* z;

static SCIP_CONSHDLR* conshdlr;
static SCIP_CONSEXPR_NLHDLR* nlhdlr = NULL;

#define EXPECTFEQ(a,b) cr_expect_float_eq(a, b, 1e-6, "%s = %g != %g (dif %g)", #a, a, b, ABS(a-b))

/* creates scip, problem, includes expression constraint handler, creates and adds variables */
static
void setup(void)
{
   int h;
   SCIP_CONSHDLRDATA* conshdlrdata;

   SCIP_CALL( SCIPcreate(&scip) );

   /* include cons_expr: this adds the operator handlers and nonlinear handlers; get quadratic handler and conshdlr */
   SCIP_CALL( SCIPincludeConshdlrExpr(scip) );

   conshdlr = SCIPfindConshdlr(scip, "expr");
   cr_assert_not_null(conshdlr);
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   cr_assert_not_null(conshdlrdata);

   /* get quadratic nlhdlr, disable all others except for default */
   for( h = 0; h < conshdlrdata->nnlhdlrs; ++h )
      if( strcmp(SCIPgetConsExprNlhdlrName(conshdlrdata->nlhdlrs[h]), "quadratic") == 0 )
      {
         nlhdlr = conshdlrdata->nlhdlrs[h];
      }
      else if( strcmp(SCIPgetConsExprNlhdlrName(conshdlrdata->nlhdlrs[h]), "default") != 0 )
      {
         conshdlrdata->nlhdlrs[h]->enabled = FALSE;
      }
   cr_assert_not_null(nlhdlr);

   /* we still want to test nlhdlr_quadratic estimating convex quadratics */
   SCIP_CALL( SCIPsetBoolParam(scip, "constraints/expr/nlhdlr/convex/cvxquadratic", FALSE) );

   /* create problem */
   SCIP_CALL( SCIPcreateProbBasic(scip, "test_problem") );

   /* go to SOLVING stage */
   SCIP_CALL( SCIPsetIntParam(scip, "presolving/maxrounds", 0) );
   SCIP_CALL( TESTscipSetStage(scip, SCIP_STAGE_SOLVING, TRUE) );

   SCIP_CALL( SCIPcreateVarBasic(scip, &x, "x", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &y, "y", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &w, "w", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &z, "z", -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPaddVar(scip, x) );
   SCIP_CALL( SCIPaddVar(scip, y) );
   SCIP_CALL( SCIPaddVar(scip, w) );
   SCIP_CALL( SCIPaddVar(scip, z) );
}

/* releases variables, frees scip */
static
void teardown(void)
{
   SCIP_CALL( SCIPreleaseVar(scip, &x) );
   SCIP_CALL( SCIPreleaseVar(scip, &y) );
   SCIP_CALL( SCIPreleaseVar(scip, &w) );
   SCIP_CALL( SCIPreleaseVar(scip, &z) );
   SCIP_CALL( SCIPfree(&scip) );

   BMSdisplayMemory();
   //BMScheckEmptyMemory();
   cr_assert_eq(BMSgetMemoryUsed(), 0, "Memory is leaking!!");
}

/* detects x^2 + x as quadratic expression */
Test(nlhdlrquadratic, detectandfree1, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_CONSEXPR_EXPRENFO_METHOD participatingexpected;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;
   SCIP_VAR* var;

   /* skip when no ipopt */
   if( ! SCIPisIpoptAvailableIpopt() )
      return;

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"<x>^2 + <x>", NULL, &expr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;

   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, NULL, &enforcing, &participating, &nlhdlrexprdata) );
   participatingexpected = SCIP_CONSEXPR_EXPRENFO_SEPABELOW | SCIP_CONSEXPR_EXPRENFO_ACTIVITY;
   cr_expect_eq(participating, participatingexpected, "expecting %d got %d\n", participatingexpected, participating);
   cr_assert(enforcing & SCIP_CONSEXPR_EXPRENFO_SEPABELOW);
   cr_assert(!(enforcing & SCIP_CONSEXPR_EXPRENFO_SEPAABOVE));
   cr_assert_eq(participating, enforcing);
   cr_assert_not_null(nlhdlrexprdata);

   cr_expect_eq(nlhdlrexprdata->quaddata->nlinexprs, 0, "Expecting 0 linear expr, got %d\n", nlhdlrexprdata->quaddata->nlinexprs);
   cr_expect_eq(nlhdlrexprdata->quaddata->nquadexprs, 1, "Expecting 1 quadratic terms, got %d\n", nlhdlrexprdata->quaddata->nquadexprs);
   cr_expect_eq(nlhdlrexprdata->quaddata->nbilinexprterms, 0, "Expecting 0 bilinear terms, got %d\n", nlhdlrexprdata->quaddata->nbilinexprterms);

   SCIP_CONSEXPR_QUADEXPRTERM quad;
   quad = nlhdlrexprdata->quaddata->quadexprterms[0];
   cr_assert_not_null(quad.expr);
   var = SCIPgetConsExprExprAuxVar(quad.expr);
   fprintf(stderr, "x = %s, quad.expr's auxvar %s\n", SCIPvarGetName(x), SCIPvarGetName(var));
   cr_expect_eq(var, x, "Expecting var %s in quad term, got %s\n", SCIPvarGetName(x), SCIPvarGetName(var));
   cr_expect_eq(1.0, quad.lincoef, "Expecting lincoef %g in quad term, got %g\n", 1.0, quad.lincoef);
   cr_expect_eq(1.0, quad.sqrcoef, "Expecting sqrcoef %g in quad term, got %g\n", 1.0, quad.sqrcoef);

   /* register enforcer info in expr and free */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(expr->enfos), 1) );
   SCIP_CALL( SCIPallocBlockMemory(scip, &(expr->enfos[0])) );
   expr->enfos[0]->nlhdlr = nlhdlr;
   expr->enfos[0]->nlhdlrexprdata = nlhdlrexprdata;
   expr->nenfos = 1;
   expr->enfos[0]->issepainit = FALSE;

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

/* detects x^2 + 2*x cos(y x^2) + cos(y x^2)^2 <= 1 as convex quadratic expression:
 * simplify yields x^2 + 2 * x cos(x^2 y) + cos(x^2 y)^2 <= 1 --> should detect x^2 + 2 x * w + w^2
 */
Test(nlhdlrquadratic, detectandfree2, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_CONSEXPR_EXPRENFO_METHOD participatingexpected;
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* cosexpr;
   SCIP_CONS* cons;
   SCIP_Bool success;
   SCIP_Bool infeasible;

   /* skip when no ipopt */
   if( ! SCIPisIpoptAvailableIpopt() )
      return;

   /* create expression, simplify it and find common subexpressions*/
   success = FALSE;
   SCIP_CALL( SCIPparseCons(scip, &cons, (char*)"[expr] <test>: <x>^2 + 2 * <x> * cos(<y> * <x>^2) + cos(<y> * <x>^2)^2 <= 1", TRUE, TRUE,
            TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, &success) );
   cr_assert(success);

   success = FALSE;
   SCIP_CALL( canonicalizeConstraints(scip, conshdlr, &cons, 1, SCIP_PRESOLTIMING_ALWAYS, &infeasible, NULL, NULL, NULL) );
   cr_assert(!infeasible);

   /* get expr and work with it */
   expr = SCIPgetExprConsExpr(scip, cons);

   /* get cosine expression */
   cr_assert_eq(SCIPgetConsExprExprNChildren(expr), 3);
   cosexpr = SCIPgetConsExprExprChildren(expr)[1]; /*  x * cos(x^2 y) */
   cosexpr = SCIPgetConsExprExprChildren(cosexpr)[1]; /* cos(x^2 y) */
   cr_assert_str_eq(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(cosexpr)), "cos", "expecting cos got %s\n",
         SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(cosexpr)));
   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, NULL, &enforcing, &participating, &nlhdlrexprdata) );
   participatingexpected = SCIP_CONSEXPR_EXPRENFO_SEPABELOW | SCIP_CONSEXPR_EXPRENFO_ACTIVITY;
   cr_expect_eq(participating, participatingexpected, "expecting %d got %d\n", participatingexpected, participating);
   cr_assert(enforcing & SCIP_CONSEXPR_EXPRENFO_SEPABELOW);
   cr_assert(!(enforcing & SCIP_CONSEXPR_EXPRENFO_SEPAABOVE));
   cr_assert_not_null(nlhdlrexprdata);

   cr_expect_eq(nlhdlrexprdata->quaddata->nlinexprs, 0, "Expecting 0 linear vars, got %d\n", nlhdlrexprdata->quaddata->nlinexprs);
   cr_expect_eq(nlhdlrexprdata->quaddata->nquadexprs, 2, "Expecting 2 quadratic terms, got %d\n", nlhdlrexprdata->quaddata->nquadexprs);
   cr_expect_eq(nlhdlrexprdata->quaddata->nbilinexprterms, 1, "Expecting 1 bilinear terms, got %d\n", nlhdlrexprdata->quaddata->nbilinexprterms);

   /* x var */
   SCIP_CONSEXPR_QUADEXPRTERM quad;
   quad = nlhdlrexprdata->quaddata->quadexprterms[0];
   cr_assert_not_null(quad.expr);
   cr_expect_eq(x, SCIPgetConsExprExprAuxVar(quad.expr), "Expecting var %s in quad term, got %s\n",
         SCIPvarGetName(x), SCIPvarGetName(SCIPgetConsExprExprAuxVar(quad.expr)));
   cr_expect_eq(0.0, quad.lincoef, "Expecting lincoef %g in quad term, got %g\n", 0.0, quad.lincoef);
   cr_expect_eq(1.0, quad.sqrcoef, "Expecting sqrcoef %g in quad term, got %g\n", 1.0, quad.sqrcoef);

   /* expr cos(x^2 y) is quadratic */
   quad = nlhdlrexprdata->quaddata->quadexprterms[1];
   cr_assert_not_null(quad.expr);
   cr_expect_eq(cosexpr, quad.expr);
   cr_expect_eq(0.0, quad.lincoef, "Expecting lincoef %g in quad term, got %g\n", 0.0, quad.lincoef);
   cr_expect_eq(1.0, quad.sqrcoef, "Expecting sqrcoef %g in quad term, got %g\n", 0.0, quad.sqrcoef);
   cr_expect(SCIPgetConsExprExprNAuxvarUses(quad.expr) > 0, "cos expr should have auxiliary variable!\n");


   SCIP_CONSEXPR_BILINEXPRTERM bilin;
   bilin = nlhdlrexprdata->quaddata->bilinexprterms[0];
   cr_assert_not_null(bilin.expr1);
   cr_assert_not_null(bilin.expr2);
   cr_expect_eq(SCIPgetConsExprExprAuxVar(bilin.expr1), x, "Expecting expr's auxvar %s in bilin term, got %s\n",
         SCIPvarGetName(x), SCIPvarGetName(SCIPgetConsExprExprAuxVar(bilin.expr1)));
   cr_expect_eq(bilin.expr2, cosexpr);
   cr_expect_eq(2.0, bilin.coef, "Expecting bilinear coef of %g, got %g\n", 2.0, bilin.coef);

   /* register nlhdlr info in expr and free */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(expr->enfos), 1) );
   SCIP_CALL( SCIPallocBlockMemory(scip, &(expr->enfos[0])) );
   expr->enfos[0]->nlhdlr = nlhdlr;
   expr->enfos[0]->nlhdlrexprdata = nlhdlrexprdata;
   expr->nenfos = 1;
   expr->enfos[0]->issepainit = FALSE;

   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

/* properly detect quadratic expression in exp(abs(log(x^2 + 2 * x*y + y^2))) <= 1 */
Test(nlhdlrquadratic, detectandfree3, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONS* cons;
   SCIP_Bool success;
   SCIP_Bool infeasible;

   /* create expression and simplify it */
   success = FALSE;
   SCIP_CALL( SCIPparseCons(scip, &cons, (char*)"[expr] <test>: exp(abs(log(<x>^2 + 2 * <x> * <y> + <y> + 2 * <y>^2))) <= 1", TRUE, TRUE,
            TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, &success) );
   cr_assert(success);

   /* adds locks which are needed for detectNlhdlrs */
   SCIP_CALL( SCIPaddConsLocks(scip, cons, 1, 0) );

   SCIP_CALL( canonicalizeConstraints(scip, conshdlr, &cons, 1, SCIP_PRESOLTIMING_ALWAYS, &infeasible, NULL, NULL, NULL) );
   cr_assert_not(infeasible);

   /* call detection method -> this registers the nlhdlr */
   SCIP_CALL( detectNlhdlrs(scip, conshdlr, &cons, 1) );

   /* get expr and work with it */
   expr = SCIPgetExprConsExpr(scip, cons);

   /* expr is exponential expr */
   cr_assert_eq(SCIPgetConsExprExprNChildren(expr), 1);
   cr_assert_str_eq(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), "exp", "expecting exp got %s\n",
         SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)));
   cr_assert(expr->nauxvaruses > 0);

   /* expr is abs expr */
   expr = SCIPgetConsExprExprChildren(expr)[0];
   cr_assert_eq(SCIPgetConsExprExprNChildren(expr), 1);
   cr_assert_str_eq(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), "abs", "expecting abs got %s\n",
         SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)));
   cr_assert(expr->nauxvaruses > 0);

   /* expr is log expr */
   expr = SCIPgetConsExprExprChildren(expr)[0];
   cr_assert_eq(SCIPgetConsExprExprNChildren(expr), 1);
   cr_assert_str_eq(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), "log", "expecting log got %s\n",
         SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)));
   cr_assert(expr->nauxvaruses > 0);

   /* expr is sum expr */
   expr = SCIPgetConsExprExprChildren(expr)[0];
   cr_assert_eq(SCIPgetConsExprExprNChildren(expr), 4);
   cr_assert_str_eq(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), "sum", "expecting sum got %s\n",
         SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)));
   cr_assert(expr->nauxvaruses > 0);

#if 0
   /* it should be identified that child should not have aux vars because of locks */
   for( int i = 0; i < SCIPgetConsExprExprNChildren(expr); ++i )
   {
      SCIP_CONSEXPR_EXPR* child = SCIPgetConsExprExprChildren(expr)[i];
      cr_expect_null(child->auxvar);
   }
#endif

   /* quadratic terms */
   SCIP_CONSEXPR_QUADEXPRTERM quad;
   cr_expect_eq(2, expr->enfos[0]->nlhdlrexprdata->quaddata->nquadexprs);

   /* x var */
   quad = expr->enfos[0]->nlhdlrexprdata->quaddata->quadexprterms[0];
   cr_assert_not_null(quad.expr);
   cr_expect_eq(x, SCIPgetConsExprExprAuxVar(quad.expr), "Expecting expr auxvar %s in quad term, got %s\n",
         SCIPvarGetName(x), SCIPvarGetName(SCIPgetConsExprExprAuxVar(quad.expr)));
   cr_expect_eq(0.0, quad.lincoef, "Expecting lincoef %g in quad term, got %g\n", 0.0, quad.lincoef);
   cr_expect_eq(1.0, quad.sqrcoef, "Expecting sqrcoef %g in quad term, got %g\n", 1.0, quad.sqrcoef);

   /* y var */
   quad = expr->enfos[0]->nlhdlrexprdata->quaddata->quadexprterms[1];
   cr_assert_not_null(quad.expr);
   cr_expect_eq(y, SCIPgetConsExprExprAuxVar(quad.expr), "Expecting expr auxvar %s in quad term, got %s\n",
         SCIPvarGetName(y), SCIPvarGetName(SCIPgetConsExprExprAuxVar(quad.expr)));
   cr_expect_eq(1.0, quad.lincoef, "Expecting lincoef %g in quad term, got %g\n", 0.0, quad.lincoef);
   cr_expect_eq(2.0, quad.sqrcoef, "Expecting sqrcoef %g in quad term, got %g\n", 1.0, quad.sqrcoef);

   /* bilinear term */
   SCIP_CONSEXPR_BILINEXPRTERM bilin;
   cr_expect_eq(1, expr->enfos[0]->nlhdlrexprdata->quaddata->nbilinexprterms);
   bilin = expr->enfos[0]->nlhdlrexprdata->quaddata->bilinexprterms[0];
   cr_assert_not_null(bilin.expr1);
   cr_assert_not_null(bilin.expr2);
   cr_expect_eq(2.0, bilin.coef, "Expecting bilincoef %g in quad term, got %g\n", 2.0, bilin.coef);
   cr_expect_eq(SCIPgetConsExprExprAuxVar(bilin.expr1), y);
   cr_expect_eq(SCIPgetConsExprExprAuxVar(bilin.expr2), x);

   /* remove locks */
   SCIP_CALL( SCIPaddConsLocks(scip, cons, -1, 0) );

   /* disable cons, so it can be deleted */
   SCIP_CALL( consDisableExpr(scip, conshdlr, cons) );

   SCIP_CALL( SCIPreleaseCons(scip, &cons) );
}

/* x^2 + y^2 + w*z should not be handled by this nlhandler */
Test(nlhdlrquadratic, notpropagablequadratic1, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"<x>^2 + <y>^2 + <w>*<z>", NULL, &expr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect_not(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;

   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, FALSE, &enforcing, &participating, &nlhdlrexprdata) );

   /* shouldn't have detected anything -> provides nothing */
   cr_expect_eq(participating, SCIP_CONSEXPR_EXPRENFO_NONE);
   cr_assert_eq(enforcing, SCIP_CONSEXPR_EXPRENFO_NONE);
   cr_expect_null(nlhdlrexprdata);

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

/* log^2 x + sin^2 y + cos^2 z should not be handled by this nlhandler */
Test(nlhdlrquadratic, notpropagable2, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"log(<x>)^2 + sin(<y>)^2 + cos(<z>)^2", NULL, &expr) );
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;

   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, FALSE, &enforcing, &participating, &nlhdlrexprdata) );

   /* shouldn't have detected anything -> provides nothing */
   cr_expect_eq(participating, SCIP_CONSEXPR_EXPRENFO_NONE);
   cr_assert_eq(enforcing, SCIP_CONSEXPR_EXPRENFO_NONE);
   cr_expect_null(nlhdlrexprdata);

   /* no auxiliary variables */
   cr_expect_eq(3, SCIPgetConsExprExprNChildren(expr));
   for( int i = 0; i < SCIPgetConsExprExprNChildren(expr); i++ )
      cr_expect_null(SCIPgetConsExprExprAuxVar(SCIPgetConsExprExprChildren(expr)[i]));

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

/* x^2 + y^2 + z^2 * x, should only provide propagation:
 * Note: we use this expression because variables are automatically detected to be
 * common subexpressions. Since we cannot call detect common subexpression to a given expression
 * as easily as calling simplify, we content with this work around
 * The alternative would be to create a constraint and canonilize it, then get the expression
 * and call the detection method of the quadratic to this expression. This is the cleanest way
 * and probably the way it should be done (TODO)
 */
Test(nlhdlrquadratic, onlyPropagation, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"<x>^2 + <y>^2 + <z>^2 * <x>", NULL, &expr) );
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, FALSE, &enforcing, &participating, &nlhdlrexprdata) );

   cr_expect_eq(participating, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", participating);
   cr_expect_eq(enforcing, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", enforcing);
   cr_expect_not_null(nlhdlrexprdata);

   /* no auxiliary variables should have been created */
   cr_expect_eq(4, SCIPgetNVars(scip), "got %d\n", SCIPgetNVars(scip));

   /* register enforcer info in expr and free */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(expr->enfos), 1) );
   SCIP_CALL( SCIPallocBlockMemory(scip, &(expr->enfos[0])) );
   expr->enfos[0]->nlhdlr = nlhdlr;
   expr->enfos[0]->nlhdlrexprdata = nlhdlrexprdata;
   expr->nenfos = 1;
   expr->enfos[0]->issepainit = FALSE;

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

/* We test propagation with the following quadratic expression
 *
 */
Test(nlhdlrquadratic, factorize, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"-<z>*<x> + <y>*<z>", NULL, &expr) );
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* detect */
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, FALSE, &enforcing, &participating, &nlhdlrexprdata) );

   cr_expect_eq(participating, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", participating);
   cr_expect_eq(enforcing, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", enforcing);
   cr_expect_not_null(nlhdlrexprdata);

   /* no auxiliary variables should have been created */
   cr_expect_eq(4, SCIPgetNVars(scip), "got %d\n", SCIPgetNVars(scip));

   /* check internal structure */
   cr_expect_eq(expr->children[0]->children[0], nlhdlrexprdata->quaddata->quadexprterms[0].expr); /* x should be first */
   cr_expect_eq(expr->children[0]->children[1], nlhdlrexprdata->quaddata->quadexprterms[1].expr); /* then z */
   cr_expect_eq(expr->children[1]->children[0], nlhdlrexprdata->quaddata->quadexprterms[2].expr); /* finally y */
   cr_expect_eq(nlhdlrexprdata->quaddata->bilinexprterms[0].expr1, nlhdlrexprdata->quaddata->bilinexprterms[1].expr1); /* z should be the first on both */

#if 0
   /* interval evaluate */
   SCIPintervalSetEntire(SCIP_INTERVAL_INFINITY, &interval);
   SCIP_CALL( SCIPevalConsExprExprInterval(scip, conshdlr, expr, 0, NULL, NULL) );
   SCIP_CALL( nlhdlrIntevalQuadratic(scip, nlhdlr, expr, nlhdlrexprdata, &interval, NULL, NULL) );

   cr_expect_float_eq(interval.inf, matinf, 1e-7); cr_expect_leq(interval.inf, matinf);
   cr_expect_float_eq(interval.sup, matsup, 1e-7); cr_expect_geq(interval.sup, matsup);

   /* test reverse propagation */
   {
      SCIP_Bool infeasible = FALSE;
      int nreductions = 0;
      exprinterval.inf = 35;
      exprinterval.sup = 35;
      SCIP_CALL( SCIPdismantleConsExprExpr(scip, NULL, expr) );
      SCIP_CALL( nlhdlrReversepropQuadratic(scip, conshdlr, nlhdlr, expr, nlhdlrexprdata, exprinterval, &infeasible, &nreductions) );
      SCIP_CALL( SCIPdismantleConsExprExpr(scip, NULL, expr) );
      cr_expect_eq(nreductions, 2);
      cr_expect_not(infeasible);
      cr_expect_float_eq(SCIPvarGetLbLocal(z), -0.0741996, 1e-7);
      cr_expect_float_eq(SCIPvarGetUbLocal(x), -0.928007, 1e-6);
   }
#endif

   /* register enforcer info in expr and free */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(expr->enfos), 1) );
   SCIP_CALL( SCIPallocBlockMemory(scip, &(expr->enfos[0])) );
   expr->enfos[0]->nlhdlr = nlhdlr;
   expr->enfos[0]->nlhdlrexprdata = nlhdlrexprdata;
   expr->nenfos = 1;
   expr->enfos[0]->issepainit = FALSE;

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

/* We test propagation with the following quadratic expression
 * x^2 - 3.1*x*y + 12.2*w*z + w^2 + 1.3*x*z - 4.8754*z^2 - 0.5*y^2 - 17.1*x + 22.02*y + 5*z - w
 * In the nlhdlr data we store the variables according to the order x < z < y < w because of repetition
 * FORWARD (or INTEVAL)
 * to compute an approximation of the max and min of this quadratic function,
 * it replaces it with the sum of univariate interval quadratics:
 * qx(x) + qy(y) + qw(w) + qz(z)
 * where
 * qx(x) = x^2 - 3.1*x*y - 17.1*x  + 1.3*x*z  -> x^2 + x*[-3.1*y - 17.1 + 1.3 * z]
 * qz(z) = -4.8754*z^2 + 5*z + 12.2*z*w       -> -4.8754*z^2 + z * [5 + 12.2*w]
 * qy(y) = -0.5*y^2 + 22.02*y                 -> -0.5*y^2 + 22.02*y
 * qw(w) = w^2 - w                            -> w^2 - 1.0 * w
 * Then computes the maximum and minimum of each q and adds them.
 *
 * BACKWARDS see mathematica code below. Now that we use the intervals of qx, qy, qz, qw
 * as computed by the forward propagation and not just doing the interval evaluation!
 *
 * Mathematicas code to generate answer: not very smart
 *
 *
Ix=Interval[{-101/100,101/100}];Iy=Interval[{7/100,9/100}];Iz=Interval[{-9/10,7/10}];Iw=Interval[{149/100,151/100}];
q[x_,y_,z_,w_]=x^2-3.1*x*y+12.2*z*w+w^2+16.3*z*x-4.8754*z^2-0.5*y^2-17.1*x+22.02*y+5*z-w;
qx[x_,y_,z_]=x^2+x*(-3.1*y-17.1+16.3*z);
qz[z_,w_]=-4.8754*z^2+z*(5+12.2*w);
qy[y_]=-0.5*y^2+22.02*y;
qw[w_]=w^2-1*w;
SetPrecision[q[Ix,Iy,Iz,Iw],15]
QXU=MaxValue[{qx[x,y,z],Element[{x},Ix]&&Element[{y},Iy]&&Element[{z},Iz]},{x,y,z}];
QYU=MaxValue[{qy[y],Element[{y},Iy]},{y}];
QWU=MaxValue[{qw[w],Element[{w},Iw]},{w}];
QZU=MaxValue[{qz[z,w],Element[{z},Iz]&&Element[{w},Iw]},{z,w}];
QXL=MinValue[{qx[x,y,z],Element[{x},Ix]&&Element[{y},Iy]&&Element[{z},Iz]},{x,y,z}];
QYL=MinValue[{qy[y],Element[{y},Iy]},{y}];
QWL=MinValue[{qw[w],Element[{w},Iw]},{w}];
QZL=MinValue[{qz[z,w],Element[{z},Iz]&&Element[{w},Iw]},{z,w}];
SetPrecision[QXL+QYL+QWL+QZL,20] (*computes lower bound*)
SetPrecision[QXU+QYU+QWU+QZU,20] (*computes upper bound*)
QX=Interval[{QXL,QXU}];QY=Interval[{QYL,QYU}];QW=Interval[{QWL,QWU}];QZ=Interval[{QZL,QZU}];
(*reverse propagation*)
Iq=Interval[{35,35}];
(* Propagating qx: first standard then bilinear terms *)
rhsx=Iq-(QY+QW+QZ); rx=Reduce[Exists[{y,z},Element[{y},Iy]&&Element[{z},Iz]&&Element[{qx[x,y,z]},rhsx]],Reals];
newIx=Interval[{MinValue[x,(rx)&&Element[{x},Ix],x],MaxValue[x,(rx)&&Element[{x},Ix],x]}]; (*improves x*)
Print["new Ix : ",newIx]

bilinxrhs = Interval[{MinValue[y/x-x,Element[{x},newIx]&&Element[{y},rhsx],{x,y}],MaxValue[y/x-x,Element[{x},newIx]&&Element[{y},rhsx],{x,y}]}];
Print["bilinxrhs ", bilinxrhs, " compare to ", rhsx/newIx - newIx]
consxforyz=Element[{-3.1*y-17.1+16.3*z},bilinxrhs];
newIy=Interval[{MinValue[y,consxforyz&&Element[{y},Iy]&&Element[{z},Iz],{y,z}],MaxValue[y,consxforyz&&Element[{y},Iy]&&Element[{z},Iz],{y,z}]}]; (* doesn't improve y*)
Print["new Iy : ",newIy]
newIz=Interval[{MinValue[z,consxforyz&&Element[{y},Iy]&&Element[{z},Iz],{y,z}],MaxValue[z,consxforyz&&Element[{y},Iy]&&Element[{z},Iz],{y,z}]}]; (*improves z*)
Print["new Iz : ",newIz]
(* Propagating qz *)
rhsz=Iq-(QX+QY+QW); rz=Reduce[Exists[{w},Element[{w},Iw]&&Element[{qz[z,w]},rhsz]],Reals];
newIz=Interval[{MinValue[z,(rz)&&Element[{z},newIz],z],MaxValue[z,(rz)&&Element[{z},newIz],z]}];
Print["new Iz : ",newIz]

(* 0 is in newIz so we don't do the following *)
(*conszforw=Element[{5+12.2*w},rhsz/newIz - newIz];
newIw=Interval[{MinValue[w,conszforw&&Element[{w},Iw],{w}],MaxValue[w,conszforw&&Element[{w},Iw],{w}]}]; (* doesn't improve y*)
Print["new Iw : ",newIw]*)
newIw=Iw;
(* Propagating qy *)
rhsy=Iq-(QX+QW+QZ); ry=Reduce[Element[{qy[y]},rhsy],Reals];
newIy=Interval[{MinValue[y,(ry)&&Element[{y},newIy],y],MaxValue[y,(ry)&&Element[{y},newIy],y]}];
Print["new Iy : ",newIy]
(* Propagating qw *)
rhsw=Iq-(QX+QZ+QY); rw=Reduce[Element[{qw[w]},rhsw],Reals];
newIw=Interval[{MinValue[w,(rw)&&Element[{w},newIw],w],MaxValue[w,(rw)&&Element[{w},newIw],w]}];
Print["new Iw : ",newIw]
 */
Test(nlhdlrquadratic, propagation_inteval, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_INTERVAL interval;
   SCIP_CONS* cons;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;

   /* set bounds: is important to do it here so that interval evaluations work */
   SCIPchgVarLb(scip, x, -1.01); SCIPchgVarUb(scip, x, 1.01);
   SCIPchgVarLb(scip, y,  0.07); SCIPchgVarUb(scip, y, 0.09);
   SCIPchgVarLb(scip, z,  -0.9); SCIPchgVarUb(scip, z,  0.7);
   SCIPchgVarLb(scip, w,  1.49); SCIPchgVarUb(scip, w, 1.51);

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"<x>^2 - 3.1*<x>*<y> + 12.2*<z>*<w> + <w>^2 + 16.3*<z>*<x> - 4.8754*<z>^2 - 0.5*<y>^2 - 17.1*<x> + 22.02*<y> + 5*<z> - <w>", NULL, &expr) );
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* the reverse propagation test requires that var-exprs get their activity updated immediately when the var bounds are updated
    * this happens when the var boundchange event is processed, which only happens for variables that are used in a constraint
    */
   SCIP_CALL( SCIPcreateConsExprBasic(scip, &cons, "cons", expr, -SCIPinfinity(scip), SCIPinfinity(scip)) );
   SCIP_CALL( SCIPaddCons(scip, cons) );
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, FALSE, &enforcing, &participating, &nlhdlrexprdata) );

   cr_expect_eq(participating, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", participating);
   cr_expect_eq(enforcing, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", enforcing);
   cr_expect_not_null(nlhdlrexprdata);

   /* no auxiliary variables should have been created */
   cr_expect_eq(4, SCIPgetNVars(scip), "got %d\n", SCIPgetNVars(scip));

   /* check internal sorting of factors */
   for( int i = 0; i < nlhdlrexprdata->quaddata->nbilinexprterms; ++ i)
   {
      /* x always first */
      cr_expect(SCIPgetConsExprExprAuxVar(nlhdlrexprdata->quaddata->bilinexprterms[i].expr2) != x);
      /* w never first */
      cr_expect(SCIPgetConsExprExprAuxVar(nlhdlrexprdata->quaddata->bilinexprterms[i].expr1) != w);

      /* z can only be second to x */
      if( SCIPgetConsExprExprAuxVar(nlhdlrexprdata->quaddata->bilinexprterms[i].expr2) == z )
         cr_expect(SCIPgetConsExprExprAuxVar(nlhdlrexprdata->quaddata->bilinexprterms[i].expr1) == x);

      /* y can only be first to w */
      if( SCIPgetConsExprExprAuxVar(nlhdlrexprdata->quaddata->bilinexprterms[i].expr1) == y )
         cr_expect(SCIPgetConsExprExprAuxVar(nlhdlrexprdata->quaddata->bilinexprterms[i].expr1) == w);
   }

   /* interval evaluate */
   SCIP_CALL( SCIPevalConsExprExprActivity(scip, conshdlr, expr, &interval, FALSE) );
   SCIPintervalSetEntire(SCIP_INTERVAL_INFINITY, &interval);
   SCIP_CALL( nlhdlrIntevalQuadratic(scip, nlhdlr, expr, nlhdlrexprdata, &interval, NULL, NULL) );

   EXPECTFEQ(interval.inf, -54.1092139000245);
   EXPECTFEQ(interval.sup, 50.1438939955093);

   /* test reverse propagation */
   {
      int nreductions = 0;
      SCIP_INTERVAL bounds;
      infeasible = FALSE;
      SCIPintervalSet(&bounds, 35);
      SCIP_CALL( SCIPdismantleConsExprExpr(scip, NULL, expr) );
      SCIP_CALL( nlhdlrReversepropQuadratic(scip, conshdlr, nlhdlr, expr, nlhdlrexprdata, bounds, &infeasible, &nreductions) );
      SCIP_CALL( SCIPdismantleConsExprExpr(scip, NULL, expr) );
      cr_expect_eq(nreductions, 3); /* three because the z improved twice */
      cr_expect_not(infeasible);

      EXPECTFEQ(SCIPvarGetLbLocal(z), -0.0485777477946283);
      EXPECTFEQ(SCIPvarGetUbLocal(z), 0.0198745061962769);
      EXPECTFEQ(SCIPvarGetUbLocal(x), -0.559537393062365);
   }


   /* register enforcer info in expr and free */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(expr->enfos), 1) );
   SCIP_CALL( SCIPallocBlockMemory(scip, &(expr->enfos[0])) );
   expr->enfos[0]->nlhdlr = nlhdlr;
   expr->enfos[0]->nlhdlrexprdata = nlhdlrexprdata;
   expr->nenfos = 1;
   expr->enfos[0]->issepainit = FALSE;

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

/* code to generate an answer:
 * MinValue[y/x + 1/2*x, Element[{x}, Ix] && Element[{y}, Ir], {x, y}]
 * MaxValue[y/x + 1/2*x, Element[{x}, Ix] && Element[{y}, Ir], {x, y}]
 * for some intervals Ix and Ir (one can also change the coef 1/2 above)
 */
Test(nlhdlrquadratic, bilin_rhs_range, .init = setup, .fini = teardown)
{
   SCIP_INTERVAL rhs;
   SCIP_INTERVAL exprdom;
   SCIP_INTERVAL range;

   rhs.inf = -8.0;
   rhs.sup = -4.0;

   exprdom.inf = 1.0;
   exprdom.sup = 10.0;

   computeRangeForBilinearProp(exprdom, 0.25, rhs, &range);
   EXPECTFEQ(range.inf, -8.25);
   EXPECTFEQ(range.sup, -2.0);

   exprdom.inf = -10.0;
   exprdom.sup = -1.0;

   computeRangeForBilinearProp(exprdom, 0.25, rhs, &range);
   EXPECTFEQ(range.inf, 2.0);
   EXPECTFEQ(range.sup, 8.25);

   rhs.inf = -8.0;
   rhs.sup = 4.0;

   computeRangeForBilinearProp(exprdom, 0.25, rhs, &range);
   EXPECTFEQ(range.inf, -3.75);
   EXPECTFEQ(range.sup, 8.25);

   computeRangeForBilinearProp(exprdom, 0.0, rhs, &range);
   EXPECTFEQ(range.inf, -4.0);
   EXPECTFEQ(range.sup, 8.0);

   rhs.inf = 0.0;
   rhs.sup = 4.0;

   computeRangeForBilinearProp(exprdom, -0.5, rhs, &range);
   EXPECTFEQ(range.inf, -5.4);
   EXPECTFEQ(range.sup, -0.5);

   exprdom.inf = -1.0;
   exprdom.sup = 10.0;

   computeRangeForBilinearProp(exprdom, 0.25, rhs, &range);
   cr_expect(SCIPintervalIsEntire(SCIP_INTERVAL_INFINITY, range));
}

/* test propagation of x*y + z + z^2; this is interesting to see how reverse propagation handles the term x*y.
 *
 * x in [1,5], y in [1, +inf], and z in [4, 5].
 *
 * Backward propagation:
 *
Ix = Interval[{1, 5}]; Iy = Interval[{1, 5}]; Iz = Interval[{4, 5}];
cons = x*y + z - z^2 == 10;
xmax = MaxValue[{x, Element[{x}, Ix] && y >= 1 && Element[{z}, Iz] && cons}, {x, y, z}]
xmin = MinValue[{x, Element[{x}, Ix] && y >= 1 && Element[{z}, Iz] && cons}, {x, y, z}]
ymax = MaxValue[{y, Element[{x}, Ix] && y >= 1 && Element[{z}, Iz] && cons}, {x, y, z}]
ymin = MinValue[{y, Element[{x}, Ix] && y >= 1 && Element[{z}, Iz] && cons}, {x, y, z}]
zmax = MaxValue[{z, Element[{x}, Ix] && y >= 1 && Element[{z}, Iz] && cons}, {x, y, z}]
zmin = MinValue[{z, Element[{x}, Ix] && y >= 1 && Element[{z}, Iz] && cons}, {x, y, z}]
 *
 */
/* FIXME: nlhdlr_quadratic now treats x*y as an argument, so detect and propagation calls for the x*y expression need to be added here */
#if SCIP_DISABLED_CODE
Test(nlhdlrquadratic, propagation_freq1vars, .init = setup, .fini = teardown)
{
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata = NULL;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* simplified;
   SCIP_CONSEXPR_EXPRENFO_METHOD enforcing;
   SCIP_CONSEXPR_EXPRENFO_METHOD participating;
   SCIP_Bool changed = FALSE;
   SCIP_Bool infeasible;
   SCIP_INTERVAL interval;

   /* set bounds: is important to do it here so that interval evaluations work */
   SCIPchgVarLb(scip, x, 1.0); SCIPchgVarUb(scip, x, 5.0);
   SCIPchgVarLb(scip, y, 1.0);
   SCIPchgVarLb(scip, z, 4.0); SCIPchgVarUb(scip, z, 5.0);

   /* create expression and simplify it: note it fails if not simplified, the order matters! */
   SCIP_CALL( SCIPparseConsExprExpr(scip, conshdlr, (char*)"<y>*<x> + <z> - <z>^2", NULL, &expr) );
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPsimplifyConsExprExpr(scip, conshdlr, expr, &simplified, &changed, &infeasible) );
   cr_expect(changed);
   cr_expect_not(infeasible);
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   expr = simplified;
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");


   /* detect */
   enforcing = SCIP_CONSEXPR_EXPRENFO_NONE;
   participating = SCIP_CONSEXPR_EXPRENFO_NONE;
   SCIP_CALL( nlhdlrDetectQuadratic(scip, conshdlr, nlhdlr, expr, FALSE, &enforcing, &participating, &nlhdlrexprdata) );

   cr_expect_eq(participating, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", participating);
   cr_expect_eq(enforcing, SCIP_CONSEXPR_EXPRENFO_ACTIVITY, "got %d\n", enforcing);
   cr_expect_not_null(nlhdlrexprdata);

   /* no auxiliary variables should have been created */
   cr_expect_eq(4, SCIPgetNVars(scip), "got %d\n", SCIPgetNVars(scip));

   /* interval evaluate */
   SCIP_CALL( SCIPevalConsExprExprActivity(scip, conshdlr, expr, &interval, FALSE, FALSE) );
   //SCIPintervalSetEntire(SCIP_INTERVAL_INFINITY, &interval);
   //SCIP_CALL( nlhdlrIntevalQuadratic(scip, nlhdlr, expr, nlhdlrexprdata, &interval, NULL, FALSE, NULL) );

   //cr_expect_float_eq(interval.inf, matinf, 1e-7, "got %f, expected %f\n", interval.inf, matinf); cr_expect_leq(interval.inf, matinf);
   //cr_expect_float_eq(interval.sup, matsup, 1e-7, "got %f, expected %f\n", interval.sup, matsup); cr_expect_geq(interval.sup, matsup);

   /* test reverse propagation */
   {
      int nreductions = 0;
      infeasible = FALSE;
      SCIPintervalSet(&interval, 10);
      SCIP_CALL( SCIPdismantleConsExprExpr(scip, NULL, expr) );
      SCIP_CALL( nlhdlrReversepropQuadratic(scip, conshdlr, nlhdlr, expr, nlhdlrexprdata, interval, &infeasible, &nreductions) );
      SCIP_CALL( SCIPdismantleConsExprExpr(scip, NULL, expr) );
      cr_expect_eq(nreductions, 2);
      cr_expect_not(infeasible);
   }

   /* check result */
   {
      int i;
      SCIP_Real expectedlowerbounds[] = {1.0, 4.4, 4.0};
      SCIP_Real expectedupperbounds[] = {5.0, 30.0, 5.0};
      SCIP_VAR* vars[] = {x, y, z};

      for( i = 0; i < (int)(sizeof(vars)/sizeof(SCIP_VAR*)); ++i )
      {
         cr_expect_float_eq(SCIPvarGetLbLocal(vars[i]), expectedlowerbounds[i], 1e-7, "var %s expecting %g, got %g\n",
               SCIPvarGetName(vars[i]), expectedlowerbounds[i], SCIPvarGetLbLocal(vars[i]));
         cr_expect_float_eq(SCIPvarGetUbLocal(vars[i]), expectedupperbounds[i], 1e-7, "var %s expecting %g, got %g\n",
               SCIPvarGetName(vars[i]), expectedupperbounds[i], SCIPvarGetUbLocal(vars[i]));
      }
   }

   /* register enforcer info in expr and free */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(expr->enfos), 1) );
   SCIP_CALL( SCIPallocBlockMemory(scip, &(expr->enfos[0])) );
   expr->enfos[0]->nlhdlr = nlhdlr;
   expr->enfos[0]->nlhdlrexprdata = nlhdlrexprdata;
   expr->nenfos = 1;
   expr->enfos[0]->issepainit = FALSE;

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}
#endif
