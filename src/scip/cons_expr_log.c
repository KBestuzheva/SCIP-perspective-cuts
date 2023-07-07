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

/**@file   cons_expr_log.c
 * @brief  logarithm expression handler
 * @author Stefan Vigerske
 * @author Benjamin Mueller
 * @author Ksenia Bestuzheva
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <string.h>

#include "scip/cons_expr_value.h"
#include "scip/cons_expr_log.h"
#include "scip/cons_expr_rowprep.h"

#define EXPRHDLR_NAME         "log"
#define EXPRHDLR_DESC         "logarithmic expression"
#define EXPRHDLR_PRECEDENCE  80000
#define EXPRHDLR_HASHKEY     SCIPcalcFibHash(16273.0)

/*
 * Data structures
 */

/** expression handler data */
struct SCIP_ConsExpr_ExprHdlrData
{
   SCIP_Real             minzerodistance;    /**< minimal distance from zero to enforce for child in bound tightening */
   SCIP_Bool             warnedonpole;       /**< whether we warned on enforcing a minimal non-zero bound for child */
};

/*
 * Local methods
 */


/*
 * Callback methods of expression handler
 */

/** simplifies a log expression.
 * Evaluates the logarithm function when its child is a value expression
 * TODO: split products ?
 * TODO: log(exp(*)) = *
 */
static
SCIP_DECL_CONSEXPR_EXPRSIMPLIFY(simplifyLog)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPR* child;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(simplifiedexpr != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);

   child = SCIPgetConsExprExprChildren(expr)[0];
   assert(child != NULL);

   /* check for value expression */
   if( SCIPgetConsExprExprHdlr(child) == SCIPgetConsExprExprHdlrValue(conshdlr) )
   {
      /* TODO how to handle a non-positive value? */
      assert(SCIPgetConsExprExprValueValue(child) > 0.0);

      SCIP_CALL( SCIPcreateConsExprExprValue(scip, conshdlr, simplifiedexpr, log(SCIPgetConsExprExprValueValue(child))) );
   }
   else
   {
      *simplifiedexpr = expr;

      /* we have to capture it, since it must simulate a "normal" simplified call in which a new expression is created */
      SCIPcaptureConsExprExpr(*simplifiedexpr);
   }

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRCOPYHDLR(copyhdlrLog)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPincludeConsExprExprHdlrLog(scip, consexprhdlr) );
   *valid = TRUE;

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRFREEHDLR(freehdlrLog)
{  /*lint --e{715}*/
   assert(exprhdlrdata != NULL);
   assert(*exprhdlrdata != NULL);

   SCIPfreeBlockMemory(scip, exprhdlrdata);

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRCOPYDATA(copydataLog)
{  /*lint --e{715}*/
   assert(targetexprdata != NULL);
   assert(sourceexpr != NULL);
   assert(SCIPgetConsExprExprData(sourceexpr) == NULL);

   *targetexprdata = NULL;

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRFREEDATA(freedataLog)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPsetConsExprExprData(expr, NULL);

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRPARSE(parseLog)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPR* childexpr;

   assert(expr != NULL);

   /* parse child expression from remaining string */
   SCIP_CALL( SCIPparseConsExprExpr(scip, consexprhdlr, string, endstring, &childexpr) );
   assert(childexpr != NULL);

   /* create logarithmic expression */
   SCIP_CALL( SCIPcreateConsExprExprLog(scip, consexprhdlr, expr, childexpr) );
   assert(*expr != NULL);

   /* release child expression since it has been captured by the logarithmic expression */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &childexpr) );

   *success = TRUE;

   return SCIP_OKAY;
}

/** expression point evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPREVAL(evalLog)
{  /*lint --e{715}*/
   assert(expr != NULL);
   assert(SCIPgetConsExprExprData(expr) == NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);
   assert(SCIPgetConsExprExprValue(SCIPgetConsExprExprChildren(expr)[0]) != SCIP_INVALID); /*lint !e777*/

   if( SCIPgetConsExprExprValue(SCIPgetConsExprExprChildren(expr)[0]) <= 0.0 )
   {
      SCIPdebugMsg(scip, "invalid evaluation of logarithmic expression\n");
      *val = SCIP_INVALID;
   }
   else
   {
      *val = log(SCIPgetConsExprExprValue(SCIPgetConsExprExprChildren(expr)[0]));
   }

   return SCIP_OKAY;
}

/** expression derivative evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPRBWDIFF(bwdiffLog)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPR* child;

   assert(expr != NULL);
   assert(childidx == 0);
   assert(SCIPgetConsExprExprValue(expr) != SCIP_INVALID); /*lint !e777*/

   child = SCIPgetConsExprExprChildren(expr)[0];
   assert(child != NULL);
   assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(child)), "val") != 0);
   assert(SCIPgetConsExprExprValue(child) > 0.0);

   *val = 1.0 / SCIPgetConsExprExprValue(child);

   return SCIP_OKAY;
}

/** expression interval evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPRINTEVAL(intevalLog)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRHDLRDATA* exprhdlrdata;
   SCIP_INTERVAL childinterval;

   assert(expr != NULL);
   assert(SCIPgetConsExprExprData(expr) == NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);

   exprhdlrdata = SCIPgetConsExprExprHdlrData(SCIPgetConsExprExprHdlr(expr));
   assert(exprhdlrdata != NULL);

   childinterval = SCIPgetConsExprExprActivity(scip, SCIPgetConsExprExprChildren(expr)[0]);

   /* pretend childinterval to be >= epsilon, see also reversepropLog */
   if( childinterval.inf < exprhdlrdata->minzerodistance && exprhdlrdata->minzerodistance > 0.0 )
   {
      if( !exprhdlrdata->warnedonpole && SCIPgetVerbLevel(scip) > SCIP_VERBLEVEL_NONE )
      {
         SCIPinfoMessage(scip, NULL, "Changing lower bound for child of log() from %g to %g.\n"
            "Check your model formulation or use option constraints/expr/exprhdlr/log/minzerodistance to avoid this warning.\n",
            childinterval.inf, exprhdlrdata->minzerodistance);
         SCIPinfoMessage(scip, NULL, "Expression: ");
         SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), expr, NULL) );
         SCIPinfoMessage(scip, NULL, "\n");
         exprhdlrdata->warnedonpole = TRUE;
      }
      childinterval.inf = exprhdlrdata->minzerodistance;
   }

   if( SCIPintervalIsEmpty(SCIP_INTERVAL_INFINITY, childinterval) )
   {
      SCIPintervalSetEmpty(interval);
      return SCIP_OKAY;
   }

   SCIPintervalLog(SCIP_INTERVAL_INFINITY, interval, childinterval);

   return SCIP_OKAY;
}

/** expression estimation callback */
static
SCIP_DECL_CONSEXPR_EXPRESTIMATE(estimateLog)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPR* child;
   SCIP_VAR* childvar;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), "expr") == 0);
   assert(expr != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);
   assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), EXPRHDLR_NAME) == 0);
   assert(coefs != NULL);
   assert(constant != NULL);
   assert(islocal != NULL);
   assert(branchcand != NULL);
   assert(*branchcand == TRUE);
   assert(success != NULL);

   /* get expression data */
   child = SCIPgetConsExprExprChildren(expr)[0];
   assert(child != NULL);
   childvar = SCIPgetConsExprExprAuxVar(child);
   assert(childvar != NULL);

   *coefs = 0.0;
   *constant = 0.0;
   *success = TRUE;

   if( overestimate )
   {
      SCIP_Real refpoint;

      /* get reference point */
      refpoint = SCIPgetSolVal(scip, sol, childvar);
      if( !SCIPisPositive(scip, refpoint) )
      {
         /* if refpoint is 0 (then lb=0 probably) or below, then slope is infinite, then try to move away from 0 */
         if( SCIPisZero(scip, SCIPvarGetUbLocal(childvar)) )
         {
            *success = FALSE;
            return SCIP_OKAY;
         }

         if( SCIPvarGetUbLocal(childvar) < 0.2 )
            refpoint = 0.5 * SCIPvarGetLbLocal(childvar) + 0.5 * SCIPvarGetUbLocal(childvar);
         else
            refpoint = 0.1;
      }

      SCIPaddLogLinearization(scip, refpoint, SCIPvarIsIntegral(childvar), coefs, constant, success);
      *islocal = FALSE; /* linearization are globally valid */
      *branchcand = FALSE;
   }
   else
   {
      SCIP_Real lb;
      SCIP_Real ub;

      lb = SCIPvarGetLbLocal(childvar);
      ub = SCIPvarGetUbLocal(childvar);

      SCIPaddLogSecant(scip, lb, ub, coefs, constant, success);
      *islocal = TRUE; /* secants are only valid locally */
   }

   return SCIP_OKAY;
}

/** init sepa callback that initializes LP for a logarithm expression */
static
SCIP_DECL_CONSEXPR_EXPRINITSEPA(initsepaLog)
{
   SCIP_Real refpointsover[3] = {SCIP_INVALID, SCIP_INVALID, SCIP_INVALID};
   SCIP_Bool overest[4] = {TRUE, TRUE, TRUE, FALSE};
   SCIP_CONSEXPR_EXPR* child;
   SCIP_VAR* childvar;
   SCIP_Real lb;
   SCIP_Real ub;
   SCIP_ROWPREP* rowprep;
   SCIP_Real constant;
   SCIP_Bool success;
   int i;
   SCIP_ROW* row;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), "expr") == 0);
   assert(expr != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);
   assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), EXPRHDLR_NAME) == 0);

   /* get expression data */
   child = SCIPgetConsExprExprChildren(expr)[0];
   assert(child != NULL);
   childvar = SCIPgetConsExprExprAuxVar(child);
   assert(childvar != NULL);

   lb = SCIPvarGetLbLocal(childvar);
   ub = SCIPvarGetUbLocal(childvar);

   if( SCIPisEQ(scip, lb, ub) )
      return SCIP_OKAY;

   /* adjust lb */
   lb = MAX(lb, MIN(0.5 * lb + 0.5 * ub, 0.1));

   if( overestimate )
   {
      refpointsover[0] = lb;
      refpointsover[1] = SCIPisInfinity(scip, ub) ? lb + 2.0 : (lb + ub) / 2;
      refpointsover[2] = SCIPisInfinity(scip, ub) ? lb + 20.0 : ub;
   }

   *infeasible = FALSE;

   for( i = 0; i < 4; ++i )
   {
      if( (overest[i] && !overestimate) || (!overest[i] && (!underestimate || SCIPisInfinity(scip, ub))) )
         continue;

      assert(!overest[i] || (SCIPisLE(scip, refpointsover[i], ub) && SCIPisGE(scip, refpointsover[i], lb))); /*lint !e661*/

      SCIP_CALL( SCIPcreateRowprep(scip, &rowprep, overest[i] ? SCIP_SIDETYPE_LEFT : SCIP_SIDETYPE_RIGHT, !overest[i]) );
      SCIP_CALL( SCIPensureRowprepSize(scip, rowprep, 1) );
      *(rowprep->coefs) = 0.0;
      constant = 0.0;
      success = TRUE;

      if( overest[i] )
      {
         assert(i < 3);
         SCIPaddLogLinearization(scip, refpointsover[i], SCIPvarIsIntegral(childvar), rowprep->coefs, &constant, &success); /*lint !e661*/
      }
      else
         SCIPaddLogSecant(scip, lb, ub, rowprep->coefs, &constant, &success);

      if( success )
      {
         rowprep->nvars = 1;
         rowprep->vars[0] = childvar;
         rowprep->side = -constant;

         /* add auxiliary variable */
         SCIP_CALL( SCIPaddRowprepTerm(scip, rowprep, SCIPgetConsExprExprAuxVar(expr), -1.0) );

         /* straighten out numerics */
         SCIP_CALL( SCIPcleanupRowprep2(scip, rowprep, NULL, SCIP_CONSEXPR_CUTMAXRANGE, SCIPgetHugeValue(scip),
               &success) );
      }

      if( success )
      {
         /* add the cut */
         SCIP_CALL( SCIPgetRowprepRowCons(scip, &row, rowprep, cons) );
         SCIP_CALL( SCIPaddRow(scip, row, FALSE, infeasible) );
         SCIP_CALL( SCIPreleaseRow(scip, &row) );
      }

      SCIPfreeRowprep(scip, &rowprep);

      if( *infeasible )
         break;
   }

   return SCIP_OKAY;
}

/** expression reverse propagation callback */
static
SCIP_DECL_CONSEXPR_EXPRREVERSEPROP(reversepropLog)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRHDLRDATA* exprhdlrdata;
   SCIP_INTERVAL childbound;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);
   assert(nreductions != NULL);

   exprhdlrdata = SCIPgetConsExprExprHdlrData(SCIPgetConsExprExprHdlr(expr));
   assert(exprhdlrdata != NULL);

   *nreductions = 0;

   /* f = log(c0) -> c0 = exp(f) */
   SCIPintervalExp(SCIP_INTERVAL_INFINITY, &childbound, bounds);

   /* force child lower bound to be at least epsilon away from 0
    * this can help a lot in enforcement (try ex8_5_3)
    * child being equal 0 is already forbidden, so making it strictly greater-equal epsilon enforces
    * and hopefully doesn't introduce much problems
    * if childbound.sup < epsilon, too, then this will result in a cutoff
    */
   if( childbound.inf < exprhdlrdata->minzerodistance )
   {
      SCIPdebugMsg(scip, "Pushing child lower bound from %g to %g; upper bound remains at %g\n", childbound.inf, SCIPepsilon(scip), childbound.sup);

      if( !exprhdlrdata->warnedonpole && SCIPgetVerbLevel(scip) > SCIP_VERBLEVEL_NONE )
      {
         SCIPinfoMessage(scip, NULL, "Changing lower bound for child of log() from %g to %g.\n"
            "Check your model formulation or use option constraints/expr/exprhdlr/log/minzerodistance to avoid this warning.\n",
            childbound.inf, exprhdlrdata->minzerodistance);
         SCIPinfoMessage(scip, NULL, "Expression: ");
         SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), expr, NULL) );
         SCIPinfoMessage(scip, NULL, "\n");
         exprhdlrdata->warnedonpole = TRUE;
      }

      childbound.inf = exprhdlrdata->minzerodistance;
   }

   /* try to tighten the bounds of the child node */
   SCIP_CALL( SCIPtightenConsExprExprInterval(scip, conshdlr, SCIPgetConsExprExprChildren(expr)[0], childbound, infeasible, nreductions) );

   return SCIP_OKAY;
}

/** expression hash callback */
static
SCIP_DECL_CONSEXPR_EXPRHASH(hashLog)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);
   assert(hashkey != NULL);
   assert(childrenhashes != NULL);

   *hashkey = EXPRHDLR_HASHKEY;
   *hashkey ^= childrenhashes[0];

   return SCIP_OKAY;
}

/** expression curvature detection callback */
static
SCIP_DECL_CONSEXPR_EXPRCURVATURE(curvatureLog)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(childcurv != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) == 1);

   /* expression is concave if child is concave, expression cannot be linear or convex */
   if( exprcurvature == SCIP_EXPRCURV_CONCAVE )
   {
      *childcurv = SCIP_EXPRCURV_CONCAVE;
      *success = TRUE;
   }
   else
      *success = FALSE;

   return SCIP_OKAY;
}

/** expression monotonicity detection callback */
static
SCIP_DECL_CONSEXPR_EXPRMONOTONICITY(monotonicityLog)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(result != NULL);
   assert(childidx == 0);

   *result = SCIP_MONOTONE_INC;

   return SCIP_OKAY;
}

/** creates the handler for logarithmic expression and includes it into the expression constraint handler */
SCIP_RETCODE SCIPincludeConsExprExprHdlrLog(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr        /**< expression constraint handler */
   )
{
   SCIP_CONSEXPR_EXPRHDLR* exprhdlr;
   SCIP_CONSEXPR_EXPRHDLRDATA* exprhdlrdata;

   SCIP_CALL( SCIPallocClearBlockMemory(scip, &exprhdlrdata) );

   SCIP_CALL( SCIPincludeConsExprExprHdlrBasic(scip, consexprhdlr, &exprhdlr, EXPRHDLR_NAME, EXPRHDLR_DESC,
         EXPRHDLR_PRECEDENCE, evalLog, exprhdlrdata) );
   assert(exprhdlr != NULL);

   SCIP_CALL( SCIPsetConsExprExprHdlrCopyFreeHdlr(scip, consexprhdlr, exprhdlr, copyhdlrLog, freehdlrLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCopyFreeData(scip, consexprhdlr, exprhdlr, copydataLog, freedataLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrSimplify(scip, consexprhdlr, exprhdlr, simplifyLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrParse(scip, consexprhdlr, exprhdlr, parseLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrIntEval(scip, consexprhdlr, exprhdlr, intevalLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrSepa(scip, consexprhdlr, exprhdlr, initsepaLog, NULL, estimateLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrReverseProp(scip, consexprhdlr, exprhdlr, reversepropLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrHash(scip, consexprhdlr, exprhdlr, hashLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrBwdiff(scip, consexprhdlr, exprhdlr, bwdiffLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCurvature(scip, consexprhdlr, exprhdlr, curvatureLog) );
   SCIP_CALL( SCIPsetConsExprExprHdlrMonotonicity(scip, consexprhdlr, exprhdlr, monotonicityLog) );

   SCIP_CALL( SCIPaddRealParam(scip, "constraints/expr/exprhdlr/" EXPRHDLR_NAME "/minzerodistance",
      "minimal distance from zero to enforce for child in bound tightening",
      &exprhdlrdata->minzerodistance, FALSE, SCIPepsilon(scip), 0.0, 1.0, NULL, NULL) );

   return SCIP_OKAY;
}

/** creates a logarithmic expression */
SCIP_RETCODE SCIPcreateConsExprExprLog(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr,       /**< expression constraint handler */
   SCIP_CONSEXPR_EXPR**  expr,               /**< pointer where to store expression */
   SCIP_CONSEXPR_EXPR*   child               /**< single child */
   )
{
   assert(expr != NULL);
   assert(child != NULL);
   assert(SCIPfindConsExprExprHdlr(consexprhdlr, EXPRHDLR_NAME) != NULL);

   SCIP_CALL( SCIPcreateConsExprExpr(scip, expr, SCIPfindConsExprExprHdlr(consexprhdlr, EXPRHDLR_NAME), NULL, 1, &child) );

   return SCIP_OKAY;
}
