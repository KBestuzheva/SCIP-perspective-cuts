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

/**@file   cons_expr_xyz.c
 * @brief  handler for xyz expressions
 * @author Benjamin Mueller
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/cons_expr_xyz.h"
#include "scip/cons_expr.h"

/* fundamental expression handler properties */
#define EXPRHDLR_NAME         "xyz"
#define EXPRHDLR_DESC         "expression handler template"
#define EXPRHDLR_PRECEDENCE   0
#define EXPRHDLR_HASHKEY      SCIPcalcFibHash(1.0)

/*
 * Data structures
 */

/* TODO: fill in the necessary data */

/** expression data */
struct SCIP_ConsExpr_ExprData
{
};

/** expression handler data */
struct SCIP_ConsExpr_ExprHdlrData
{
};

/*
 * Local methods
 */

/* TODO: put your local methods here, and declare them static */

/*
 * Callback methods of expression handler
 */

/** expression handler copy callback */
static
SCIP_DECL_CONSEXPR_EXPRCOPYHDLR(copyhdlrXyz)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression handler free callback */
static
SCIP_DECL_CONSEXPR_EXPRFREEHDLR(freehdlrXyz)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** simplifies a xyz expression */
static
SCIP_DECL_CONSEXPR_EXPRSIMPLIFY(simplifyXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression compare callback */
static
SCIP_DECL_CONSEXPR_EXPRCOMPARE(compareXyz)
{  /*lint --e{715}*/
   assert(expr1 != NULL);
   assert(expr2 != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return 0;
}

/** expression data copy callback */
static
SCIP_DECL_CONSEXPR_EXPRCOPYDATA(copydataXyz)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression data free callback */
static
SCIP_DECL_CONSEXPR_EXPRFREEDATA(freedataXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression print callback */
static
SCIP_DECL_CONSEXPR_EXPRPRINT(printXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression parse callback */
static
SCIP_DECL_CONSEXPR_EXPRPARSE(parseXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression (point-) evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPREVAL(evalXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression derivative evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPRBWDIFF(bwdiffXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression interval evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPRINTEVAL(intevalXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** separation initialization callback */
static
SCIP_DECL_CONSEXPR_EXPRINITSEPA(initSepaXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** separation deinitialization callback */
static
SCIP_DECL_CONSEXPR_EXPREXITSEPA(exitSepaXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression under/overestimation callback */
static
SCIP_DECL_CONSEXPR_EXPRESTIMATE(estimateXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression reverse propagation callback */
static
SCIP_DECL_CONSEXPR_EXPRREVERSEPROP(reversepropXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** xyz hash callback */
static
SCIP_DECL_CONSEXPR_EXPRHASH(hashXyz)
{  /*lint --e{715}*/
   assert(expr != NULL);
   assert(EXPRHDLR_HASHKEY != 0.0);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression curvature detection callback */
static
SCIP_DECL_CONSEXPR_EXPRCURVATURE(curvatureXyz)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(curvature != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression monotonicity detection callback */
static
SCIP_DECL_CONSEXPR_EXPRMONOTONICITY(monotonicityXyz)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(result != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression integrality detection callback */
static
SCIP_DECL_CONSEXPR_EXPRINTEGRALITY(integralityXyz)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(isintegral != NULL);

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** expression branching score callback */
static
SCIP_DECL_CONSEXPR_EXPRBRANCHSCORE(branchscoreXyz)
{
   assert(scip != NULL);
   assert(expr != NULL);
   assert(success != NULL);

   *success = FALSE;

   SCIPerrorMessage("method of xyz constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** creates the handler for xyz expressions and includes it into the expression constraint handler */
SCIP_RETCODE SCIPincludeConsExprExprHdlrXyz(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr        /**< expression constraint handler */
   )
{
   SCIP_CONSEXPR_EXPRHDLRDATA* exprhdlrdata;
   SCIP_CONSEXPR_EXPRHDLR* exprhdlr;

   /* create expression handler data */
   exprhdlrdata = NULL;

   /* TODO: create and store expression handler specific data here */

   /* include expression handler */
   SCIP_CALL( SCIPincludeConsExprExprHdlrBasic(scip, consexprhdlr, &exprhdlr, EXPRHDLR_NAME, EXPRHDLR_DESC,
         EXPRHDLR_PRECEDENCE, evalXyz, exprhdlrdata) );
   assert(exprhdlr != NULL);

   SCIP_CALL( SCIPsetConsExprExprHdlrCopyFreeHdlr(scip, consexprhdlr, exprhdlr, copyhdlrXyz, freehdlrXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCopyFreeData(scip, consexprhdlr, exprhdlr, copydataXyz, freedataXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrSimplify(scip, consexprhdlr, exprhdlr, simplifyXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCompare(scip, consexprhdlr, exprhdlr, compareXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrPrint(scip, consexprhdlr, exprhdlr, printXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrParse(scip, consexprhdlr, exprhdlr, parseXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrIntEval(scip, consexprhdlr, exprhdlr, intevalXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrSepa(scip, consexprhdlr, exprhdlr, initSepaXyz, exitSepaXyz, estimateXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrReverseProp(scip, consexprhdlr, exprhdlr, reversepropXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrHash(scip, consexprhdlr, exprhdlr, hashXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrBwdiff(scip, consexprhdlr, exprhdlr, bwdiffXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCurvature(scip, consexprhdlr, exprhdlr, curvatureXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrMonotonicity(scip, consexprhdlr, exprhdlr, monotonicityXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrIntegrality(scip, consexprhdlr, exprhdlr, integralityXyz) );
   SCIP_CALL( SCIPsetConsExprExprHdlrBranchscore(scip, consexprhdlr, exprhdlr, branchscoreXyz) );

   return SCIP_OKAY;
}

/** creates a xyz expression */
SCIP_RETCODE SCIPcreateConsExprExprXyz(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr,       /**< expression constraint handler */
   SCIP_CONSEXPR_EXPR**  expr,               /**< pointer where to store expression */
   int                   nchildren,          /**< number of children */
   SCIP_CONSEXPR_EXPR**  children            /**< children (can be NULL if nchildren is 0) */
   )
{
   SCIP_CONSEXPR_EXPRHDLR* exprhdlr;
   SCIP_CONSEXPR_EXPRDATA* exprdata;

   assert(consexprhdlr != NULL);
   assert(expr != NULL);

   exprhdlr = SCIPfindConsExprExprHdlr(consexprhdlr, EXPRHDLR_NAME);

   if( exprhdlr != NULL )
   {
      SCIPerrorMessage("could not find %s expression handler -> abort\n", EXPRHDLR_NAME);
      SCIPABORT();
      return SCIP_ERROR;
   }

   /* create expression data */
   exprdata = NULL;

   /* TODO: create and store expression specific data here */

   /* create expression */
   SCIP_CALL( SCIPcreateConsExprExpr(scip, expr, exprhdlr, exprdata, nchildren, children) );

   return SCIP_OKAY;
}
