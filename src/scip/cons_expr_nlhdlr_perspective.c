/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cons_expr_nlhdlr_perspective.c
 * @brief  perspective nonlinear handler
 * @author Ksenia Bestuzheva
 */

#include <string.h>

#include "scip/cons_varbound.h"
#include "scip/cons_expr_nlhdlr_perspective.h"
#include "scip/cons_expr.h"
#include "scip/cons_expr_var.h"
#include "scip/scip_sol.h"
#include "scip/cons_expr_iterator.h"
#include "scip/cons_expr_rowprep.h"
#include "cons_expr_sum.h"

/* fundamental nonlinear handler properties */
#define NLHDLR_NAME               "perspective"
#define NLHDLR_DESC               "perspective handler for expressions"
#define NLHDLR_DETECTPRIORITY     -20 /**< detect last so that to make use of what other handlers detected */
#define NLHDLR_ENFOPRIORITY       125 /**< enforce first because perspective cuts are always stronger */

#define DEFAULT_MAXPROPROUNDS     1     /**< maximal number of propagation rounds in probing */
#define DEFAULT_MINDOMREDUCTION   0.1   /**< minimal relative reduction in a variable's domain for applying probing */
#define DEFAULT_MINVIOLPROBING    1e-05 /**< minimal violation w.r.t. auxiliary variables for applying probing */
#define DEFAULT_PROBINGONLYINSEPA TRUE  /**< whether to do probing only in separation loop */
#define DEFAULT_PROBINGFREQ       1     /**< probing frequency (-1 - no probing, 0 - root node only) */
#define DEFAULT_CONVEXONLY        FALSE /**< whether perspective cuts are added only for convex expressions */
#define DEFAULT_TIGHTENBOUNDS     TRUE  /**< whether variable semicontinuity is used to tighten variable bounds */
#define DEFAULT_ADJREFPOINT       FALSE /**< whether to adjust the reference point if indicator is not 1 */
#define DEFAULT_BIGMCUTS          FALSE /**< whether to strengthen cuts for constraints with big-M structure */

/** translates x to 2^x for non-negative integer x */
#define POWEROFTWO(x) (0x1u << (x))

/*
 * Data structures
 */

/** data structure to store information of a semicontinuous variable
 *
 * For a variable x (not stored in the struct), this stores the data of nbnds implications
 *   bvars[i] = 0 -> x = vals[i]
 *   bvars[i] = 1 -> lbs[i] <= x <= ubs[i]
 * where bvars[i] are binary variables.
 */
struct SCVarData
{
   SCIP_Real*            vals0;              /**< values of the variable when the corresponding bvars[i] = 0 */
   SCIP_Real*            lbs1;               /**< global lower bounds of the variable when the corresponding bvars[i] = 1 */
   SCIP_Real*            ubs1;               /**< global upper bounds of the variable when the corresponding bvars[i] = 1 */
   SCIP_VAR**            bvars;              /**< the binary variables on which the variable domain depends */
   int                   nbnds;              /**< number of suitable on/off bounds the var has */
   int                   bndssize;           /**< size of the arrays */
};
typedef struct SCVarData SCVARDATA;

/** nonlinear handler expression data
 *
 * For an expression expr (not stored in the struct), this stores the data of nindicators implications
 *   indicators[i] = 0 -> expr = exprvals[0]
 * where indicators[i] is an indicator (binary) variable, corresponding to some bvars entry in SCVarData.
 *
 * Also stores the variables the expression depends on.
 */
struct SCIP_ConsExpr_NlhdlrExprData
{
   SCIP_Real*            exprvals0;          /**< 'off' values of the expression for each indicator variable */
   SCIP_VAR**            vars;               /**< expression variables (both original and auxiliary) */
   int                   nvars;              /**< total number of variables in the expression */
   int                   varssize;           /**< size of the vars array */
   SCIP_VAR**            indicators;         /**< all indicator variables for the expression */
   int                   nindicators;        /**< number of indicator variables */
   SCIP_Bool             onlybigm;
};

/** nonlinear handler data */
struct SCIP_ConsExpr_NlhdlrData
{
   SCIP_HASHMAP*         scvars;             /**< maps semicontinuous variables to their on/off bounds (SCVarData) */

   /* parameters */
   int                   maxproprounds;      /**< maximal number of propagation rounds in probing */
   SCIP_Real             mindomreduction;    /**< minimal relative reduction in a variable's domain for applying probing */
   SCIP_Real             minviolprobing;     /**< minimal violation w.r.t. auxiliary variables for applying probing */
   SCIP_Bool             probingonlyinsepa;  /**< whether to do probing only in separation loop */
   int                   probingfreq;        /**< if and when to do probing */
   SCIP_Bool             convexonly;         /**< whether perspective cuts are added only for convex expressions */
   SCIP_Bool             tightenbounds;      /**< whether variable semicontinuity is used to tighten variable bounds */
   SCIP_Bool             adjrefpoint;        /**< whether to adjust the reference point if indicator is not 1 */
   SCIP_Bool             bigmcuts;           /**< whether to strengthen cuts for constraints with big-M structure */

   /* statistic counters */
   int                   ndetects;           /**< total number of expressions detected */
   int                   nconvexdetects;     /**< total number of convex expressions detected */
   int                   nnonconvexdetects;  /**< total number of nonconvex expressions detected */
   int                   nonlybigmdetects;   /**< total number of non-semicontinuous expressions detected that participate only in big-M constraints */
   int                   nbigmenfos;         /**< number of successfully separated cuts for big-M-like constraints */
};

/*
 * Local methods
 */

/*
 * Helper methods for working with nlhdlrExprData
 */

/** frees nlhdlrexprdata structure */
static
SCIP_RETCODE freeNlhdlrExprData(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata /**< nlhdlr expression data */
   )
{
   int v;

   if( nlhdlrexprdata->nindicators != 0 )
   {
      assert(nlhdlrexprdata->indicators != NULL);
      for( v = nlhdlrexprdata->nindicators - 1; v >= 0; --v )
      {
         SCIP_CALL( SCIPreleaseVar(scip, &(nlhdlrexprdata->indicators[v])) );
      }
      SCIPfreeBlockMemoryArray(scip, &(nlhdlrexprdata->indicators), nlhdlrexprdata->nindicators);
      SCIPfreeBlockMemoryArrayNull(scip, &(nlhdlrexprdata->exprvals0), nlhdlrexprdata->nindicators);
   }

   for( v = nlhdlrexprdata->nvars - 1; v >= 0; --v )
   {
      SCIP_CALL( SCIPreleaseVar(scip, &(nlhdlrexprdata->vars[v])) );
   }
   SCIPfreeBlockMemoryArrayNull(scip, &nlhdlrexprdata->vars, nlhdlrexprdata->varssize);

   return SCIP_OKAY;
}

/* remove an indicator from nlhdlr expression data */
static
void removeIndicator(
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlexprdata, /**< nlhdlr expression data */
   int                    pos                /**< position of the indicator */
   )
{
   int i;

   assert(pos >= 0 && pos < nlexprdata->nindicators);

   for( i = pos; i < nlexprdata->nindicators - 1; ++i )
   {
      nlexprdata->indicators[i] = nlexprdata->indicators[i+1];
   }

   --nlexprdata->nindicators;
}

/** adds an auxiliary variable to the vars array in nlhdlrexprdata */
static
SCIP_RETCODE addAuxVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_HASHMAP*         auxvarmap,          /**< hashmap linking auxvars to positions in nlhdlrexprdata->vars */
   SCIP_VAR*             auxvar              /**< variable to be added */
   )
{
   int pos;
   int newsize;

   assert(nlhdlrexprdata != NULL);
   assert(auxvar != NULL);

   pos = SCIPhashmapGetImageInt(auxvarmap, (void*) auxvar);

   if( pos != INT_MAX )
      return SCIP_OKAY;

   /* ensure size */
   if( nlhdlrexprdata->nvars + 1 > nlhdlrexprdata->varssize )
   {
      newsize = SCIPcalcMemGrowSize(scip, nlhdlrexprdata->nvars + 1);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &nlhdlrexprdata->vars, nlhdlrexprdata->varssize, newsize) );
      nlhdlrexprdata->varssize = newsize;
   }
   assert(nlhdlrexprdata->nvars + 1 <= nlhdlrexprdata->varssize);

   nlhdlrexprdata->vars[nlhdlrexprdata->nvars] = auxvar;
   SCIP_CALL( SCIPcaptureVar(scip, auxvar) );
   SCIP_CALL( SCIPhashmapSetImageInt(auxvarmap, (void*) auxvar, nlhdlrexprdata->nvars) );
   ++(nlhdlrexprdata->nvars);

   return SCIP_OKAY;
}

/*
 * Semicontinuous variable methods
 */

/** adds an indicator to the data of a semicontinuous variable */
static
SCIP_RETCODE addSCVarIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCVARDATA*            scvdata,            /**< semicontinuous variable data */
   SCIP_VAR*             indicator,          /**< indicator to be added */
   SCIP_Real             val0,               /**< value of the variable when indicator == 0 */
   SCIP_Real             lb1,                /**< lower bound of the variable when indicator == 1 */
   SCIP_Real             ub1                 /**< upper bound of the variable when indicator == 1 */
   )
{
   int newsize;
   int i;
   SCIP_Bool found;
   int pos;

   assert(scvdata != NULL);
   assert(indicator != NULL);

   /* find the position where to insert */
   if( scvdata->bvars == NULL )
   {
      assert(scvdata->nbnds == 0 && scvdata->bndssize == 0);
      found = FALSE;
      pos = 0;
   }
   else
   {
      found = SCIPsortedvecFindPtr((void**)scvdata->bvars, SCIPvarComp, (void*)indicator, scvdata->nbnds, &pos);
   }

   if( found )
      return SCIP_OKAY;

   /* ensure sizes */
   if( scvdata->nbnds + 1 > scvdata->bndssize )
   {
      newsize = SCIPcalcMemGrowSize(scip, scvdata->nbnds + 1);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &scvdata->bvars, scvdata->bndssize, newsize) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &scvdata->vals0, scvdata->bndssize, newsize) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &scvdata->lbs1, scvdata->bndssize, newsize) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &scvdata->ubs1, scvdata->bndssize, newsize) );
      scvdata->bndssize = newsize;
   }
   assert(scvdata->nbnds + 1 <= scvdata->bndssize);
   assert(scvdata->bvars != NULL);

   /* move entries if needed */
   for( i = scvdata->nbnds; i > pos; --i )
   {
      scvdata->bvars[i] = scvdata->bvars[i-1];
      scvdata->vals0[i] = scvdata->vals0[i-1];
      scvdata->lbs1[i] = scvdata->lbs1[i-1];
      scvdata->ubs1[i] = scvdata->ubs1[i-1];
   }

   scvdata->bvars[pos] = indicator;
   scvdata->vals0[pos] = val0;
   scvdata->lbs1[pos] = lb1;
   scvdata->ubs1[pos] = ub1;
   ++scvdata->nbnds;

   return SCIP_OKAY;
}

/** find scvardata of var and position of indicator in it
 *
 *  If indicator is not there, returns NULL.
 */
static
SCVARDATA* getSCVarDataInd(
   SCIP_HASHMAP*         scvars,             /**< hashmap linking variables to scvardata */
   SCIP_VAR*             var,                /**< variable */
   SCIP_VAR*             indicator,          /**< indicator variable */
   int*                  pos                 /**< pointer to store the position of indicator */
   )
{
   SCIP_Bool exists;
   SCVARDATA* scvdata;

   assert(var != NULL);
   assert(scvars != NULL);
   assert(indicator != NULL);

   scvdata = (SCVARDATA*) SCIPhashmapGetImage(scvars, (void*)var);
   if( scvdata != NULL )
   {
      /* look for the indicator variable */
      exists = SCIPsortedvecFindPtr((void**)scvdata->bvars, SCIPvarComp, (void*)indicator, scvdata->nbnds, pos);
      if( !exists )
         return NULL;

      return scvdata;
   }

   return NULL;
}

/** checks if a variable is semicontinuous and, if needed, updates the scvars hashmap
 *
 * A variable x is semicontinuous if its bounds depend on at least one binary variable called the indicator,
 * and indicator == 0 => x == x^0 for some real constant x^0.
 */
static
SCIP_RETCODE varIsSemicontinuous(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< the variable to check */
   SCIP_HASHMAP*         scvars,             /**< semicontinuous variable information */
   SCIP_Bool*            result              /**< buffer to store whether var is semicontinuous */
   )
{
   SCIP_Real lb0;
   SCIP_Real ub0;
   SCIP_Real lb1;
   SCIP_Real ub1;
   SCIP_Real glb;
   SCIP_Real gub;
   SCIP_Bool exists;
   int c;
   int pos;
   SCIP_VAR** vlbvars;
   SCIP_VAR** vubvars;
   SCIP_Real* vlbcoefs;
   SCIP_Real* vubcoefs;
   SCIP_Real* vlbconstants;
   SCIP_Real* vubconstants;
   int nvlbs;
   int nvubs;
   SCVARDATA* scvdata;
   SCIP_VAR* bvar;

   assert(scip != NULL);
   assert(var != NULL);
   assert(scvars != NULL);
   assert(result != NULL);

   scvdata = (SCVARDATA*) SCIPhashmapGetImage(scvars, (void*)var);
   if( scvdata != NULL )
   {
      *result = TRUE;
      return SCIP_OKAY;
   }

   vlbvars = SCIPvarGetVlbVars(var);
   vubvars = SCIPvarGetVubVars(var);
   vlbcoefs = SCIPvarGetVlbCoefs(var);
   vubcoefs = SCIPvarGetVubCoefs(var);
   vlbconstants = SCIPvarGetVlbConstants(var);
   vubconstants = SCIPvarGetVubConstants(var);
   nvlbs = SCIPvarGetNVlbs(var);
   nvubs = SCIPvarGetNVubs(var);
   glb = SCIPvarGetLbGlobal(var);
   gub = SCIPvarGetUbGlobal(var);

   *result = FALSE;

   /* Scan through lower bounds; for each binary vlbvar save the corresponding lb0 and lb1.
    * Then check if there is an upper bound with this vlbvar and save ub0 and ub1.
    * If the found bounds imply that the var value is fixed to some val0 when vlbvar = 0,
    * save vlbvar and val0 to scvdata.
    */
   for( c = 0; c < nvlbs; ++c )
   {
      if( SCIPvarGetType(vlbvars[c]) != SCIP_VARTYPE_BINARY )
         continue;

      SCIPdebugMsg(scip, "var <%s>[%f, %f] lower bound: %f <%s> %+f", SCIPvarGetName(var), glb, gub, vlbcoefs[c], SCIPvarGetName(vlbvars[c]), vlbconstants[c]);

      bvar = vlbvars[c];

      lb0 = MAX(vlbconstants[c], glb);
      lb1 = MAX(vlbconstants[c] + vlbcoefs[c], glb);

      /* look for bvar in vubvars */
      if( vubvars != NULL )
         exists = SCIPsortedvecFindPtr((void**)vubvars, SCIPvarComp, bvar, nvubs, &pos);
      else
         exists = FALSE;
      if( exists )
      { /*lint --e{644}*/
         SCIPdebugMsgPrint(scip, ", upper bound: %f <%s> %+f", vubcoefs[pos], SCIPvarGetName(vubvars[pos]), vubconstants[pos]); /*lint !e613*/

         /* save the upper bounds */
         ub0 = MIN(vubconstants[pos], gub);
         ub1 = MIN(vubconstants[pos] + vubcoefs[pos], gub);
      }
      else
      {
         /* if there is no upper bound with vubvar = bvar, use global var bounds */
         ub0 = gub;
         ub1 = gub;
      }

      /* the 'off' domain of a semicontinuous var should reduce to a single point and be different from the 'on' domain */
      SCIPdebugMsgPrint(scip, " -> <%s> in [%f, %f] (off), [%f, %f] (on)\n", SCIPvarGetName(var), lb0, ub0, lb1, ub1);
      if( SCIPisEQ(scip, lb0, ub0) && (!SCIPisEQ(scip, lb0, lb1) || !SCIPisEQ(scip, ub0, ub1)) )
      {
         if( scvdata == NULL )
         {
            SCIP_CALL( SCIPallocClearBlockMemory(scip, &scvdata) );
         }

         SCIP_CALL( addSCVarIndicator(scip, scvdata, bvar, lb0, lb1, ub1) );
      }
   }

   /* look for vubvars that have not been processed yet */
   assert(vubvars != NULL || nvubs == 0);
   for( c = 0; c < nvubs; ++c )
   {
      if( SCIPvarGetType(vubvars[c]) != SCIP_VARTYPE_BINARY )  /*lint !e613*/
         continue;

      bvar = vubvars[c];  /*lint !e613*/

      /* skip vars that are in vlbvars */
      if( vlbvars != NULL && SCIPsortedvecFindPtr((void**)vlbvars, SCIPvarComp, bvar, nvlbs, &pos) )
         continue;

      SCIPdebugMsg(scip, "var <%s>[%f, %f] upper bound: %f <%s> %+f",
         SCIPvarGetName(var), glb, gub, vubcoefs[c], SCIPvarGetName(vubvars[c]), vubconstants[c]);  /*lint !e613*/

      lb0 = glb;
      lb1 = glb;
      ub0 = MIN(vubconstants[c], gub);
      ub1 = MIN(vubconstants[c] + vubcoefs[c], gub);

      /* the 'off' domain of a semicontinuous var should reduce to a single point and be different from the 'on' domain */
      SCIPdebugMsgPrint(scip, " -> <%s> in [%f, %f] (off), [%f, %f] (on)\n", SCIPvarGetName(var), lb0, ub0, lb1, ub1);
      if( SCIPisEQ(scip, lb0, ub0) && (!SCIPisEQ(scip, lb0, lb1) || !SCIPisEQ(scip, ub0, ub1)) )
      {
         if( scvdata == NULL )
         {
            SCIP_CALL( SCIPallocClearBlockMemory(scip, &scvdata) );
         }

         SCIP_CALL( addSCVarIndicator(scip, scvdata, bvar, lb0, lb1, ub1) );
      }
   }

   if( scvdata != NULL )
   {
#ifdef SCIP_DEBUG
      SCIPdebugMsg(scip, "var <%s> has global bounds [%f, %f] and the following on/off bounds:\n", SCIPvarGetName(var), glb, gub);
      for( c = 0; c < scvdata->nbnds; ++c )
      {
         SCIPdebugMsg(scip, " c = %d, bvar <%s>: val0 = %f\n", c, SCIPvarGetName(scvdata->bvars[c]), scvdata->vals0[c]);
      }
#endif
      SCIP_CALL( SCIPhashmapInsert(scvars, var, scvdata) );
      *result = TRUE;
   }

   return SCIP_OKAY;
}

/*
 * Semicontinuous expression methods
 */

/* checks if an expression is semicontinuous
 *
 * An expression is semicontinuous if all of its nonlinear variables are semicontinuous
 * and share at least one common indicator variable
 */
static
SCIP_RETCODE exprIsSemicontinuous(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< expression constraint handler */
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,     /**< nonlinear handler data */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_CONSEXPR_EXPR*   expr,               /**< expression */
   SCIP_Bool*            res                 /**< buffer to store whether the expression is semicontinuous */
   )
{
   int v;
   SCIP_Bool var_is_sc;
   SCVARDATA* scvdata;
   SCIP_VAR* var;
   int nindicators;
   int nbnds0;
   int c;
   SCIP_VAR** indicators;
   SCIP_Bool* linear;

   *res = FALSE;

   /* constant expression is not semicontinuous; variable expressions are of no interest here */
   if( nlhdlrexprdata->nvars == 0 )
      return SCIP_OKAY;

   indicators = NULL;
   nindicators = 0;
   nbnds0 = 0;

   if( SCIPgetConsExprExprHdlr(expr) == SCIPgetConsExprExprHdlrSum(conshdlr) )
   {
      SCIP_CONSEXPR_ITERATOR* it;
      SCIP_CONSEXPR_EXPR* child;
      SCIP_CONSEXPR_EXPR* curexpr;
      int pos;
      SCIP_Bool issc;

      /* sums are treated separately because if there are variables that are non-semicontinuous but
       * appear only linearly, we still want to apply perspective to expr
       */

      SCIP_CALL( SCIPallocClearBufferArray(scip, &linear, nlhdlrexprdata->nvars) );
      SCIP_CALL( SCIPexpriteratorCreate(&it, conshdlr, SCIPblkmem(scip)) );

      for( c = 0; c < SCIPgetConsExprExprNChildren(expr); ++c )
      {
         child = SCIPgetConsExprExprChildren(expr)[c];

         if( SCIPisConsExprExprVar(child) )
         {
            var = SCIPgetConsExprExprVarVar(child);

            /* save information on semicontinuity of child */
            SCIP_CALL( varIsSemicontinuous(scip, var, nlhdlrdata->scvars, &var_is_sc) );

            /* mark the variable as linear */
            (void) SCIPsortedvecFindPtr((void**) nlhdlrexprdata->vars, SCIPvarComp, (void*) var, nlhdlrexprdata->nvars,
                  &pos);
            assert(0 <= pos && pos < nlhdlrexprdata->nvars);
            linear[pos] = TRUE;

            /* since child is a variable, go on regardless of the value of var_is_sc */
            continue;
         }

         issc = TRUE;

         SCIP_CALL( SCIPexpriteratorInit(it, child, SCIP_CONSEXPRITERATOR_DFS, FALSE) );
         curexpr = SCIPexpriteratorGetCurrent(it);

         /* all nonlinear terms of a sum should be semicontinuous in original variables */
         while( !SCIPexpriteratorIsEnd(it) )
         {
            assert(curexpr != NULL);

            if( SCIPisConsExprExprVar(curexpr) )
            {
               var = SCIPgetConsExprExprVarVar(curexpr);

               if( !SCIPvarIsRelaxationOnly(var) )
               {
                  SCIP_CALL( varIsSemicontinuous(scip, var, nlhdlrdata->scvars, &var_is_sc) );

                  if( !var_is_sc )
                  {
                     /* non-semicontinuous child which is (due to a previous check) not a var ->
                      * expr is non-semicontinuous
                      */
                     issc = FALSE;
                     break;
                  }
               }
            }
            curexpr = SCIPexpriteratorGetNext(it);
         }

         if( !issc )
         {
            SCIPexpriteratorFree(&it);
            goto TERMINATE;
         }
      }
      SCIPexpriteratorFree(&it);
   }
   else
   {
      /* non-sum expression */
      linear = NULL;

      /* all variables of a non-sum on/off expression should be semicontinuous */
      for( v = 0; v < nlhdlrexprdata->nvars; ++v )
      {
         SCIP_CALL( varIsSemicontinuous(scip, nlhdlrexprdata->vars[v], nlhdlrdata->scvars, &var_is_sc) );
         if( !var_is_sc )
            return SCIP_OKAY;
      }
   }

   /* look for common binary variables for all variables of the expression */

   SCIPdebugMsg(scip, "Array intersection for var <%s>\n", SCIPvarGetName(nlhdlrexprdata->vars[0]));
   for( v = 0; v < nlhdlrexprdata->nvars; ++v )
   {
      SCIPdebugMsg(scip, "%s; \n", SCIPvarGetName(nlhdlrexprdata->vars[v]));

      if( linear != NULL && linear[v] )
         continue;

      scvdata = (SCVARDATA*)SCIPhashmapGetImage(nlhdlrdata->scvars, (void*) nlhdlrexprdata->vars[v]);

      /* we should have exited earlier if there is a nonlinear non-semicontinuous variable */
      assert(scvdata != NULL);

      if( indicators == NULL )
      {
         SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &indicators, scvdata->bvars, scvdata->nbnds) );
         nbnds0 = scvdata->nbnds;
         nindicators = nbnds0;
      }
      else
      {
         SCIPcomputeArraysIntersectionPtr((void**)indicators, nindicators, (void**)scvdata->bvars, scvdata->nbnds,
               SCIPvarComp, (void**)indicators, &nindicators);
      }

      /* if we have found out that the intersection is empty, expr is not semicontinuous */
      if( indicators != NULL && nindicators == 0 )
      {
         SCIPfreeBlockMemoryArray(scip, &indicators, nbnds0);
         goto TERMINATE;
      }
   }

   /* this can happen if all children are linear vars and none are semicontinuous */
   if( indicators == NULL )
   {
      goto TERMINATE;
   }
   assert(nindicators > 0 && nindicators <= nbnds0);

   if( nindicators < nbnds0 )
   {
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &indicators, nbnds0, nindicators) );
   }

   for( v = 0; v < nindicators; ++v )
   {
      SCIP_CALL( SCIPcaptureVar(scip, indicators[v]) );
   }
   nlhdlrexprdata->indicators = indicators;
   nlhdlrexprdata->nindicators = nindicators;
   *res = TRUE;

 TERMINATE:
   SCIPfreeBufferArrayNull(scip, &linear);

   return SCIP_OKAY;
}

/** stores auxiliary variables in nlhdlr expression data */
static
SCIP_RETCODE saveAuxVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,     /**< nonlinear handler data */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_CONSEXPR_EXPR*   expr                /**< expression */
   )
{
   SCIP_CONSEXPR_ITERATOR* it;
   SCIP_VAR* auxvar;
   SCIP_CONSEXPR_EXPR* curexpr;
   SCIP_HASHMAP* auxvarmap;

   assert(expr != NULL);

   SCIP_CALL( SCIPhashmapCreate(&auxvarmap, SCIPblkmem(scip), 10) );
   SCIP_CALL( SCIPexpriteratorCreate(&it, conshdlr, SCIPblkmem(scip)) );

   /* iterate through the expression and add aux vars */
   SCIP_CALL( SCIPexpriteratorInit(it, expr, SCIP_CONSEXPRITERATOR_DFS, FALSE) );
   curexpr = SCIPexpriteratorGetCurrent(it);

   while( !SCIPexpriteratorIsEnd(it) )
   {
      auxvar = SCIPgetConsExprExprAuxVar(curexpr);

      if( auxvar != NULL && !SCIPisConsExprExprVar(curexpr) )
      {
         SCIP_CALL( addAuxVar(scip, nlhdlrexprdata, auxvarmap, auxvar) );
      }
      curexpr = SCIPexpriteratorGetNext(it);
   }

   SCIPexpriteratorFree(&it);
   SCIPhashmapFree(&auxvarmap);

   return SCIP_OKAY;
}

/** computes the 'off' value of the expression and the 'off' values of
  * semicontinuous auxiliary variables for each indicator variable
  */
static
SCIP_RETCODE computeOffValues(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,     /**< nonlinear handler data */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_CONSEXPR_EXPR*   expr                /**< expression */
   )
{
   SCIP_CONSEXPR_ITERATOR* it;
   SCIP_SOL* sol;
   int i;
   int v;
   int norigvars;
   SCIP_Real* origvals0;
   SCIP_VAR** origvars;
   SCVARDATA* scvdata;
   SCIP_VAR* auxvar;
   SCIP_CONSEXPR_EXPR* curexpr;
   SCIP_HASHMAP* auxvarmap;
   SCIP_Bool hasnonsc;
   int pos;

   assert(expr != NULL);

   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(nlhdlrexprdata->exprvals0), nlhdlrexprdata->nindicators) );
   SCIP_CALL( SCIPcreateSol(scip, &sol, NULL) );
   SCIP_CALL( SCIPallocBufferArray(scip, &origvals0, nlhdlrexprdata->nvars) );
   SCIP_CALL( SCIPhashmapCreate(&auxvarmap, SCIPblkmem(scip), 10) );
   SCIP_CALL( SCIPexpriteratorCreate(&it, conshdlr, SCIPblkmem(scip)) );
   SCIP_CALL( SCIPduplicateBufferArray(scip, &origvars, nlhdlrexprdata->vars, nlhdlrexprdata->nvars) );
   norigvars = nlhdlrexprdata->nvars;

   for( i = nlhdlrexprdata->nindicators - 1; i >= 0; --i )
   {
      hasnonsc = FALSE;

      /* set sol to the off value of all expr vars for this indicator */
      for( v = 0; v < norigvars; ++v )
      {
         /* set vals0[v] = 0 if var is non-sc with respect to indicators[i] - then it will not
          * contribute to exprvals0[i] since any non-sc var must be linear
          */
         scvdata = getSCVarDataInd(nlhdlrdata->scvars, origvars[v], nlhdlrexprdata->indicators[i], &pos);
         if( scvdata == NULL )
         {
            origvals0[v] = 0.0;
            hasnonsc = TRUE;
         }
         else
         {
            origvals0[v] = scvdata->vals0[pos];
         }
      }
      SCIP_CALL( SCIPsetSolVals(scip, sol, norigvars, origvars, origvals0) );
      SCIP_CALL( SCIPevalConsExprExpr(scip, conshdlr, expr, sol, 0) );

      if( SCIPgetConsExprExprValue(expr) == SCIP_INVALID ) /*lint !e777*/
      {
         SCIPdebugMsg(scip, "expression evaluation failed for %p, removing indicator %s\n",
                             (void*)expr, SCIPvarGetName(nlhdlrexprdata->indicators[i]));
         /* TODO should we fix the indicator variable to 1? */
         /* since the loop is backwards, this only modifies the already processed part of nlhdlrexprdata->indicators */
         removeIndicator(nlhdlrexprdata, i);
         continue;
      }

      nlhdlrexprdata->exprvals0[i] = SCIPgetConsExprExprValue(expr);

      /* iterate through the expression and create scvdata for aux vars */
      SCIP_CALL( SCIPexpriteratorInit(it, expr, SCIP_CONSEXPRITERATOR_DFS, FALSE) );
      curexpr = SCIPexpriteratorGetCurrent(it);

      while( !SCIPexpriteratorIsEnd(it) )
      {
         auxvar = SCIPgetConsExprExprAuxVar(curexpr);

         if( auxvar != NULL && !SCIPisConsExprExprVar(curexpr) )
         {
            SCIP_Bool issc = TRUE;
#ifndef NDEBUG
            SCIP_CONSEXPR_EXPR** childvarexprs;
            int nchildvarexprs;
            SCIP_VAR* var;
#endif

            if( hasnonsc )
            {
               /* expr is a sum with non-semicontinuous linear terms. Therefore, curexpr might be
                * non-semicontinuous. In that case the auxvar is also non-semicontinuous, so
                * we will skip on/off bounds computation.
                */
               if( SCIPgetConsExprExprHdlr(curexpr) == SCIPgetConsExprExprHdlrVar(conshdlr) )
               {
                  /* easy case: curexpr is a variable, can check semicontinuity immediately */
                  scvdata = getSCVarDataInd(nlhdlrdata->scvars, SCIPgetConsExprExprVarVar(curexpr),
                        nlhdlrexprdata->indicators[i], &pos);
                  issc = scvdata != NULL;
               }
               else if( SCIPgetConsExprExprHdlr(curexpr) != SCIPgetConsExprExprHdlrSum(conshdlr) )
               {
                  /* curexpr is a non-variable expression, so it belongs to the non-linear part of expr
                   * since the non-linear part of expr must be semicontinuous with respect to
                   * nlhdlrexprdata->indicators[i], curexpr must be semicontinuous
                   */
                  issc = TRUE;

#ifndef NDEBUG
                  SCIP_CALL( SCIPallocBufferArray(scip, &childvarexprs, norigvars) );
                  SCIP_CALL( SCIPgetConsExprExprVarExprs(scip, conshdlr, curexpr, childvarexprs, &nchildvarexprs) );

                  /* all nonlinear variables of a sum on/off term should be semicontinuous */
                  for( v = 0; v < nchildvarexprs; ++v )
                  {
                     var = SCIPgetConsExprExprVarVar(childvarexprs[v]);
                     scvdata = getSCVarDataInd(nlhdlrdata->scvars, var, nlhdlrexprdata->indicators[i], &pos);
                     assert(scvdata != NULL);

                     SCIP_CALL( SCIPreleaseConsExprExpr(scip, &childvarexprs[v]) );
                  }

                  SCIPfreeBufferArray(scip, &childvarexprs);
#endif
               }
            }

            if( issc )
            {
               /* we know that all vars are semicontinuous with respect to exprdata->indicators; it remains to:
                * - get or create the scvardata structure for auxvar
                * - if had to create scvardata, add it to scvars hashmap
                * - add the indicator and the off value (= curexpr's off value) to scvardata
                */
               scvdata = (SCVARDATA*) SCIPhashmapGetImage(nlhdlrdata->scvars, (void*)auxvar);
               if( scvdata == NULL )
               {
                  SCIP_CALL( SCIPallocClearBlockMemory(scip, &scvdata) );
                  SCIP_CALL( SCIPallocBlockMemoryArray(scip, &scvdata->bvars,  nlhdlrexprdata->nindicators) );
                  SCIP_CALL( SCIPallocBlockMemoryArray(scip, &scvdata->vals0, nlhdlrexprdata->nindicators) );
                  SCIP_CALL( SCIPallocBlockMemoryArray(scip, &scvdata->lbs1, nlhdlrexprdata->nindicators) );
                  SCIP_CALL( SCIPallocBlockMemoryArray(scip, &scvdata->ubs1, nlhdlrexprdata->nindicators) );
                  scvdata->bndssize = nlhdlrexprdata->nindicators;
                  SCIP_CALL( SCIPhashmapInsert(nlhdlrdata->scvars, auxvar, scvdata) );
               }

               SCIP_CALL( addSCVarIndicator(scip, scvdata, nlhdlrexprdata->indicators[i],
                     SCIPgetConsExprExprValue(curexpr), SCIPvarGetLbGlobal(auxvar), SCIPvarGetUbGlobal(auxvar)) );
            }

//            SCIP_CALL( addAuxVar(scip, nlhdlrexprdata, auxvarmap, auxvar) );
         }

         curexpr = SCIPexpriteratorGetNext(it);
      }
   }

   SCIPexpriteratorFree(&it);
   SCIPhashmapFree(&auxvarmap);
   SCIPfreeBufferArray(scip, &origvals0);
   SCIPfreeBufferArray(scip, &origvars);
   SCIP_CALL( SCIPfreeSol(scip, &sol) );

   return SCIP_OKAY;
}

/*
 * Probing and bound tightening methods
 */

/** go into probing and set some variable bounds */
static
SCIP_RETCODE startProbing(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,     /**< nonlinear handler data */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_VAR*             indicator,          /**< indicator variable */
   SCIP_VAR**            probingvars,        /**< array of vars whose bounds we will change in probing */
   SCIP_INTERVAL*        probingdoms,        /**< array of intervals to which bounds of probingvars will be changed in probing */
   int                   nprobingvars,       /**< number of probing vars */
   SCIP_SOL*             sol,                /**< solution to be separated */
   SCIP_SOL**            solcopy,            /**< buffer for a copy of sol before going into probing; if *solcopy == sol, then copy is created */
   SCIP_Bool*            cutoff_probing      /**< pointer to store whether indicator == 1 is infeasible */
   )
{
   int v;
   SCIP_Real newlb;
   SCIP_Real newub;
   SCIP_Bool propagate;

   propagate = SCIPgetDepth(scip) == 0;

   /* if a copy of sol has not been created yet, then create one now and copy the relevant var values from sol,
    * because sol can change after SCIPstartProbing, e.g., when linked to the LP solution
    */
   if( *solcopy == sol )
   {
      SCIP_CALL( SCIPcreateSol(scip, solcopy, NULL) );
      for( v = 0; v < nlhdlrexprdata->nvars; ++v )
      {
         SCIP_CALL( SCIPsetSolVal(scip, *solcopy, nlhdlrexprdata->vars[v], SCIPgetSolVal(scip, sol, nlhdlrexprdata->vars[v])) );
      }
      for( v = 0; v < nlhdlrexprdata->nindicators; ++v )
      {
         SCIP_CALL( SCIPsetSolVal(scip, *solcopy, nlhdlrexprdata->indicators[v], SCIPgetSolVal(scip, sol, nlhdlrexprdata->indicators[v])) );
      }
   }

   /* go into probing */
   SCIP_CALL( SCIPstartProbing(scip) );

   /* create a probing node */
   SCIP_CALL( SCIPnewProbingNode(scip) );

   /* set indicator to 1 */
   SCIP_CALL( SCIPchgVarLbProbing(scip, indicator, 1.0) );

   /* apply stored bounds */
   for( v = 0; v < nprobingvars; ++v )
   {
      newlb = SCIPintervalGetInf(probingdoms[v]);
      newub = SCIPintervalGetSup(probingdoms[v]);

      if( SCIPisGT(scip, newlb, SCIPvarGetLbLocal(probingvars[v])) || (newlb >= 0.0 && SCIPvarGetLbLocal(probingvars[v]) < 0.0) )
      {
         SCIP_CALL( SCIPchgVarLbProbing(scip, probingvars[v], newlb) );
      }
      if( SCIPisLT(scip, newub, SCIPvarGetUbLocal(probingvars[v])) || (newub <= 0.0 && SCIPvarGetUbLocal(probingvars[v]) > 0.0) )
      {
         SCIP_CALL( SCIPchgVarUbProbing(scip, probingvars[v], newub) );
      }
   }

   if( propagate )
   {
      SCIP_Longint ndomreds;

      SCIP_CALL( SCIPpropagateProbing(scip, nlhdlrdata->maxproprounds, cutoff_probing, &ndomreds) );
   }

   return SCIP_OKAY;
}

/** analyse on/off bounds on a variable for: 1) tightening bounds in probing for indicator = 1,
  * 2) fixing indicator / detecting cutoff if one or both states are infeasible,
  * 3) tightening local bounds if indicator is fixed.
  *
  * probinglb and probingub are only set if doprobing is TRUE.
  * They are either set to bounds that should be used in probing or to SCIP_INVALID if bounds on
  * var shouldn't be changed in probing.
  */
static
SCIP_RETCODE analyseVarOnoffBounds(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,     /**< nonlinear handler data */
   SCIP_VAR*             var,                /**< variable */
   SCIP_VAR*             indicator,          /**< indicator variable */
   SCIP_Bool             indvalue,           /**< indicator value for which the bounds are applied */
   SCIP_Bool*            infeas,             /**< pointer to store whether infeasibility has been detected */
   SCIP_Real*            probinglb,          /**< pointer to store the lower bound to be applied in probing */
   SCIP_Real*            probingub,          /**< pointer to store the upper bound to be applied in probing */
   SCIP_Bool             doprobing,          /**< whether we currently consider to go into probing */
   SCIP_Bool*            reduceddom          /**< pointer to store whether any variables were fixed */
   )
{
   SCVARDATA* scvdata;
   int pos;
   SCIP_Real sclb;
   SCIP_Real scub;
   SCIP_Real loclb;
   SCIP_Real locub;
   SCIP_Bool bndchgsuccess;

   assert(var != NULL);
   assert(indicator != NULL);
   assert(infeas != NULL);
   assert(reduceddom != NULL);

   /* shouldn't be called if indicator is fixed to !indvalue */
   assert((indvalue && SCIPvarGetUbLocal(indicator) > 0.5) || (!indvalue && SCIPvarGetLbLocal(indicator) < 0.5));

   *infeas = FALSE;
   *reduceddom = FALSE;
   scvdata = getSCVarDataInd(nlhdlrdata->scvars, var, indicator, &pos);
   if( doprobing )
   {
      assert(probinglb != NULL);
      assert(probingub != NULL);

      *probinglb = SCIP_INVALID;
      *probingub = SCIP_INVALID;
   }

   /* nothing to do for non-semicontinuous variables */
   if( scvdata == NULL )
   {
      return SCIP_OKAY;
   }

   sclb = indvalue ? scvdata->lbs1[pos] : scvdata->vals0[pos];
   scub = indvalue ? scvdata->ubs1[pos] : scvdata->vals0[pos];
   loclb = SCIPvarGetLbLocal(var);
   locub = SCIPvarGetUbLocal(var);

   /* use a non-redundant lower bound */
   if( SCIPisGT(scip, sclb, SCIPvarGetLbLocal(var)) || (sclb >= 0.0 && loclb < 0.0) )
   {
      /* first check for infeasibility */
      if( SCIPisFeasGT(scip, sclb, SCIPvarGetUbLocal(var)) )
      {
         SCIP_CALL( SCIPfixVar(scip, indicator, indvalue ? 0.0 : 1.0, infeas, &bndchgsuccess) );
         *reduceddom += bndchgsuccess;
         if( *infeas )
         {
            return SCIP_OKAY;
         }
      }
      else if( nlhdlrdata->tightenbounds &&
              (SCIPvarGetUbLocal(indicator) <= 0.5 || SCIPvarGetLbLocal(indicator) >= 0.5) )
      {
         /* indicator is fixed; due to a previous check, here it can only be fixed to indvalue;
          * therefore, sclb is valid for the current node
          */

         if( indvalue == 0 )
         {
            assert(sclb == scub); /*lint !e777*/
            SCIP_CALL( SCIPfixVar(scip, var, sclb, infeas, &bndchgsuccess) );
         }
         else
         {
            SCIP_CALL( SCIPtightenVarLb(scip, var, sclb, FALSE, infeas, &bndchgsuccess) );
         }
         *reduceddom += bndchgsuccess;
         if( *infeas )
         {
            return SCIP_OKAY;
         }
      }
   }

   /* use a non-redundant upper bound */
   if( SCIPisLT(scip, scub, SCIPvarGetUbLocal(var)) || (scub <= 0.0 && locub > 0.0) )
   {
      /* first check for infeasibility */
      if( SCIPisFeasLT(scip, scub, SCIPvarGetLbLocal(var)) )
      {
         SCIP_CALL( SCIPfixVar(scip, indicator, indvalue ? 0.0 : 1.0, infeas, &bndchgsuccess) );
         *reduceddom += bndchgsuccess;
         if( *infeas )
         {
            return SCIP_OKAY;
         }
      }
      else if( nlhdlrdata->tightenbounds &&
              (SCIPvarGetUbLocal(indicator) <= 0.5 || SCIPvarGetLbLocal(indicator) >= 0.5) )
      {
         /* indicator is fixed; due to a previous check, here it can only be fixed to indvalue;
          * therefore, scub is valid for the current node
          */

         if( indvalue == 0 )
         {
            assert(sclb == scub); /*lint !e777*/
            SCIP_CALL( SCIPfixVar(scip, var, sclb, infeas, &bndchgsuccess) );
         }
         else
         {
            SCIP_CALL( SCIPtightenVarUb(scip, var, scub, FALSE, infeas, &bndchgsuccess) );
         }
         *reduceddom += bndchgsuccess;
         if( *infeas )
         {
            return SCIP_OKAY;
         }
      }
   }

   /* If a bound change has been found and indvalue == TRUE, try to use the new bounds.
    * This is only done for indvalue == TRUE since this is where enfo asks other nlhdlrs to estimate,
    * and at indicator == FALSE we already only have a single point
    */
   if( doprobing && indvalue && (((scub - sclb) / (locub - loclb)) <= 1.0 - nlhdlrdata->mindomreduction ||
       (sclb >= 0.0 && loclb < 0.0) || (scub <= 0.0 && locub > 0.0)) )
   {
      *probinglb = sclb;
      *probingub = scub;
   }

   SCIPdebugMsg(scip, "%s in [%g, %g] instead of [%g, %g] (vals0 = %g)\n", SCIPvarGetName(var), sclb, scub,
                SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var), scvdata->vals0[pos]);

   return SCIP_OKAY;
}

/** looks for bound tightenings to be applied either in the current node or in probing
 *
 * Loops through both possible values of indicator and calls analyseVarOnoffBounds. Might update the *doprobing
 * flag by setting it to FALSE if:
 * - indicator is fixed or
 * - analyseVarOnoffBounds hasn't found a sufficient improvement at indicator==1.
 *
 * If *doprobing==TRUE, stores bounds suggested by analyseVarOnoffBounds in order to apply them in probing together
 * with the fixing indicator=1.
 */
static
SCIP_RETCODE analyseOnoffBounds(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,     /**< nonlinear handler data */
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_VAR*             indicator,          /**< indicator variable */
   SCIP_VAR***           probingvars,        /**< array to store variables whose bounds will be changed in probing */
   SCIP_INTERVAL**       probingdoms,        /**< array to store bounds to be applied in probing */
   int*                  nprobingvars,       /**< pointer to store number of vars whose bounds will be changed in probing */
   SCIP_Bool*            doprobing,          /**< pointer to the flag telling whether we want to do probing */
   SCIP_RESULT*          result              /**< pointer to store the result */
   )
{
   int v;
   SCIP_VAR* var;
   SCIP_Bool infeas;
   int b;
   SCIP_Real probinglb = SCIP_INVALID;
   SCIP_Real probingub = SCIP_INVALID;
   SCIP_Bool changed;
   SCIP_Bool reduceddom;

   assert(indicator != NULL);
   assert(nprobingvars != NULL);
   assert(doprobing != NULL);
   assert(result != NULL);

   changed = FALSE;

   /* no probing if indicator already fixed */
   if( SCIPvarGetUbLocal(indicator) <= 0.5 || SCIPvarGetLbLocal(indicator) >= 0.5 )
   {
      *doprobing = FALSE;
   }

   /* consider each possible value of indicator */
   for( b = 0; b < 2; ++b )
   {
      for( v = 0; v < nlhdlrexprdata->nvars; ++v )
      {
         /* nothing left to do if indicator is already fixed to !indvalue
          * (checked in the inner loop since analyseVarOnoff bounds might fix the indicator)
          */
         if( (b == 1 && SCIPvarGetUbLocal(indicator) <= 0.5) || (b == 0 && SCIPvarGetLbLocal(indicator) >= 0.5) )
         {
            *doprobing = FALSE;
            break;
         }

         var = nlhdlrexprdata->vars[v];

         SCIP_CALL( analyseVarOnoffBounds(scip, nlhdlrdata, var, indicator, b == 1, &infeas, &probinglb,
               &probingub, *doprobing, &reduceddom) );

         if( infeas )
         {
            *result = SCIP_CUTOFF;
            *doprobing = FALSE;
            return SCIP_OKAY;
         }
         else if( reduceddom )
         {
            *result = SCIP_REDUCEDDOM;
         }

         if( !(*doprobing) )
            continue;

         /* if bounds to be applied in probing have been found, store them */
         if( probinglb != SCIP_INVALID ) /*lint !e777*/
         {
            assert(probingub != SCIP_INVALID); /*lint !e777*/

            SCIP_CALL( SCIPreallocBufferArray(scip, probingvars, *nprobingvars + 1) );
            SCIP_CALL( SCIPreallocBufferArray(scip, probingdoms, *nprobingvars + 1) );
            (*probingvars)[*nprobingvars] = var;
            (*probingdoms)[*nprobingvars].inf = probinglb;
            (*probingdoms)[*nprobingvars].sup = probingub;
            ++*nprobingvars;

            changed = TRUE;
         }
      }
   }

   if( !changed )
   {
      *doprobing = FALSE;
   }

   return SCIP_OKAY;
}

/** saves local bounds on all expression variables, including auxiliary variables, obtained from propagating
 * indicator == 1 to the corresponding SCVARDATA (should only be used in the root node)
 * */
static
SCIP_RETCODE tightenOnBounds(
   SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlrexprdata, /**< nlhdlr expression data */
   SCIP_HASHMAP*         scvars,             /**< hashmap with semicontinuous variables */
   SCIP_VAR*             indicator           /**< indicator variable */
   )
{
   int v;
   SCIP_VAR* var;
   SCVARDATA* scvdata;
   int pos;
   SCIP_Real lb;
   SCIP_Real ub;

   for( v = 0; v < nlhdlrexprdata->nvars; ++v )
   {
      var = nlhdlrexprdata->vars[v];
      lb = SCIPvarGetLbLocal(var);
      ub = SCIPvarGetUbLocal(var);
      scvdata = getSCVarDataInd(scvars, var, indicator, &pos);

      if( scvdata != NULL )
      {
         scvdata->lbs1[pos] = MAX(scvdata->lbs1[pos], lb);
         scvdata->ubs1[pos] = MIN(scvdata->ubs1[pos], ub);
      }
   }

   return SCIP_OKAY;
}

/*
 * Callback methods of nonlinear handler
 */

/** nonlinear handler copy callback */
static
SCIP_DECL_CONSEXPR_NLHDLRCOPYHDLR(nlhdlrCopyhdlrPerspective)
{ /*lint --e{715}*/
   assert(targetscip != NULL);
   assert(targetconsexprhdlr != NULL);
   assert(sourcenlhdlr != NULL);
   assert(strcmp(SCIPgetConsExprNlhdlrName(sourcenlhdlr), NLHDLR_NAME) == 0);

   SCIP_CALL( SCIPincludeConsExprNlhdlrPerspective(targetscip, targetconsexprhdlr) );

   return SCIP_OKAY;
}


/** callback to free data of handler */
static
SCIP_DECL_CONSEXPR_NLHDLRFREEHDLRDATA(nlhdlrFreehdlrdataPerspective)
{ /*lint --e{715}*/
   SCIPfreeBlockMemory(scip, nlhdlrdata);

   return SCIP_OKAY;
}


/** callback to free expression specific data */
static
SCIP_DECL_CONSEXPR_NLHDLRFREEEXPRDATA(nlhdlrFreeExprDataPerspective)
{  /*lint --e{715}*/
   SCIP_CALL( freeNlhdlrExprData(scip, *nlhdlrexprdata) );
   SCIPfreeBlockMemory(scip, nlhdlrexprdata);

   return SCIP_OKAY;
}

/** callback to be called in initialization */
static
SCIP_DECL_CONSEXPR_NLHDLRINIT(nlhdlrInitPerspective)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;

   nlhdlrdata = SCIPgetConsExprNlhdlrData(nlhdlr);
   assert(nlhdlrdata != NULL);

   nlhdlrdata->ndetects = 0;
   nlhdlrdata->nconvexdetects = 0;
   nlhdlrdata->nnonconvexdetects = 0;
   nlhdlrdata->nonlybigmdetects = 0;
   nlhdlrdata->nbigmenfos = 0;

   return SCIP_OKAY;
}

/** callback to be called in deinitialization */
static
SCIP_DECL_CONSEXPR_NLHDLREXIT(nlhdlrExitPerspective)
{  /*lint --e{715}*/
   SCIP_HASHMAPENTRY* entry;
   SCVARDATA* data;
   int c;
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;

   nlhdlrdata = SCIPgetConsExprNlhdlrData(nlhdlr);
   assert(nlhdlrdata != NULL);

   if( nlhdlrdata->scvars != NULL )
   {
      for( c = 0; c < SCIPhashmapGetNEntries(nlhdlrdata->scvars); ++c )
      {
         entry = SCIPhashmapGetEntry(nlhdlrdata->scvars, c);
         if( entry != NULL )
         {
            data = (SCVARDATA*) SCIPhashmapEntryGetImage(entry);
            SCIPfreeBlockMemoryArray(scip, &data->ubs1, data->bndssize);
            SCIPfreeBlockMemoryArray(scip, &data->lbs1, data->bndssize);
            SCIPfreeBlockMemoryArray(scip, &data->vals0, data->bndssize);
            SCIPfreeBlockMemoryArray(scip, &data->bvars, data->bndssize);
            SCIPfreeBlockMemory(scip, &data);
         }
      }
      SCIPhashmapFree(&nlhdlrdata->scvars);
      assert(nlhdlrdata->scvars == NULL);
   }

   if( nlhdlrdata->ndetects > 0 )
   {
      SCIPinfoMessage(scip, NULL, "\nndetects%s = %d", SCIPgetSubscipDepth(scip) > 0 ? " (in subscip)" : "",
            nlhdlrdata->ndetects);
   }

   if( nlhdlrdata->nconvexdetects > 0 )
   {
      SCIPinfoMessage(scip, NULL, "\nnconvexdetects%s = %d", SCIPgetSubscipDepth(scip) > 0 ? " (in subscip)" : "",
            nlhdlrdata->nconvexdetects);
   }

   if( nlhdlrdata->nnonconvexdetects > 0 )
   {
      SCIPinfoMessage(scip, NULL, "\nnnonconvexdetects%s = %d", SCIPgetSubscipDepth(scip) > 0 ? " (in subscip)" : "",
            nlhdlrdata->nnonconvexdetects);
   }

   if( nlhdlrdata->nonlybigmdetects > 0 )
   {
      SCIPinfoMessage(scip, NULL, "\nnonlybigmdetects%s = %d", SCIPgetSubscipDepth(scip) > 0 ? " (in subscip)" : "",
                      nlhdlrdata->nonlybigmdetects);
   }

   if( nlhdlrdata->nbigmenfos > 0 )
   {
      SCIPinfoMessage(scip, NULL, "\nbigmenfors%s = %d", SCIPgetSubscipDepth(scip) > 0 ? " (in subscip)" : "",
                      nlhdlrdata->nbigmenfos);
   }

   return SCIP_OKAY;
}

/** determines if a constraint is big-M, that is, becomes reduntant when some indicator(s) are at 0 */
static
SCIP_RETCODE consIsBigM(
   SCIP*                 scip,               /** SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /** nonlinear constraint handler */
   SCIP_CONSEXPR_NLHDLR* nlhdlr,             /** perspective nonlinear handler */
   SCIP_CONS*            cons,               /** constraint */
   SCIP_CONSEXPR_EXPR*   expr,               /** expression */
   SCIP_VAR**            vars,               /** expression variables */
   int                   nvars,              /** number of expression variables */
   SCIP_Bool             hasnondefault,      /** whether expr has a non-default estimator */
   SCIP_Bool*            success             /** buffer to store whether the constraint is big-M */
   )
{
   SCIP_VAR** indicators;
   int nindicators;
   int indicatorssize;
   int v;
   int i;
   int j;
   int c;
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;
   SCVARDATA** scvdatas;
   SCIP_INTERVAL activity;
   SCIP_INTERVAL* childactivities;

   assert(conshdlr != NULL);
   assert(nlhdlr != NULL);
   assert(cons != NULL);
   assert(expr != NULL);
   assert(vars != NULL);
   assert(success != NULL);

   nlhdlrdata = SCIPgetConsExprNlhdlrData(nlhdlr);
   indicators = NULL;
   nindicators = 0;
   indicatorssize = 0;
   SCIP_CALL( SCIPallocBufferArray(scip, &scvdatas, nvars) );

   /* get list of all indicators */
   for( v = 0; v < nvars; ++v )
   {
      SCIP_Bool sc;

      SCIP_CALL(varIsSemicontinuous(scip, vars[v], nlhdlrdata->scvars, &sc) );
      if( !sc )
      {
         scvdatas[v] = NULL;
         continue;
      }

      scvdatas[v] = (SCVARDATA*) SCIPhashmapGetImage(nlhdlrdata->scvars, (void*)vars[v]);
      assert(scvdatas[v] != NULL);

      /* add all indicators from scvdata to indicators (i.e., make a union) */
      for( i = 0; i < scvdatas[v]->nbnds; ++i )
      {
         SCIP_Bool found;
         int pos;

         if( indicators == NULL )
         {
            assert(nindicators == 0 && indicatorssize == 0);
            found = FALSE;
            pos = 0;
         }
         else
         {
            found = SCIPsortedvecFindPtr((void**)indicators, SCIPvarComp, (void*)scvdatas[v]->bvars[i], nindicators, &pos);
         }
         if( found )
            continue;

         /* ensure sizes */
         if( nindicators + 1 > indicatorssize )
         {
            int newsize;
            newsize = SCIPcalcMemGrowSize(scip, nindicators + 1);
            SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &indicators, indicatorssize, newsize) );
            indicatorssize = newsize;
         }
         assert(nindicators + 1 <= indicatorssize);
         assert(indicators != NULL);

         /* move entries if needed */
         for( j = nindicators; j > pos; --j )
         {
            indicators[j] = indicators[j-1];
         }

         indicators[pos] = scvdatas[v]->bvars[i];
         ++nindicators;
      }
   }

   if( nindicators > 0 )
   {
      SCIP_CONSEXPR_BNDDATA scdata;

      SCIP_CALL( SCIPallocBufferArray(scip, &scdata.vars, nvars + 1) );
      SCIP_CALL( SCIPallocBufferArray(scip, &scdata.lbs, nvars + 1) );
      SCIP_CALL( SCIPallocBufferArray(scip, &scdata.ubs, nvars + 1) );

      if( SCIPgetConsExprExprHdlr(expr) == SCIPgetConsExprExprHdlrSum(conshdlr) )
      {
         SCIP_CALL(SCIPallocBufferArray(scip, &childactivities, SCIPgetConsExprExprNChildren(expr)) );
      }

      for( i = 0; i < nindicators; ++i )
      {
         SCIP_Longint ndomreds;
         SCIP_Bool cutoff;
         SCIP_Bool redundant;

         if( cons != NULL )
         {
            SCIP_Bool indincons = FALSE; /* whether indicator is one of the cons vars */

            /* fill in intevalvardata */
            for( v = 0; v < nvars; ++v )
            {
               scdata.vars[v] = vars[v];
               if( scvdatas[v] != NULL )
               {
                  SCIP_Bool found;
                  int pos;

                  found = SCIPsortedvecFindPtr((void**)scvdatas[v]->bvars, SCIPvarComp, (void*)indicators[i], scvdatas[v]->nbnds, &pos);
                  if( found )
                  {
                     scdata.lbs[v] = scvdatas[v]->vals0[pos];
                     scdata.ubs[v] = scvdatas[v]->vals0[pos];
                  }
               }
               else if( vars[v] == indicators[i] )
               {
                  indincons = TRUE;
                  scdata.lbs[v] = 0.0;
                  scdata.ubs[v] = 0.0;
               }
               else
               {
                  scdata.lbs[v] = SCIPvarGetLbGlobal(vars[v]);
                  scdata.ubs[v] = SCIPvarGetUbGlobal(vars[v]);
               }
            }

            if( !indincons )
            {
               scdata.vars[nvars] = indicators[i];
               scdata.lbs[nvars] = 0.0;
               scdata.ubs[nvars] = 0.0;
               scdata.nvars = nvars+1;
            }
            else
            {
               scdata.vars[nvars] = NULL;
               scdata.nvars = nvars;
            }

#ifdef SCIP_DEBUG
            SCIPdebugMsg(scip,  "\nscdata for indicator %s: ", SCIPvarGetName(indicators[i]));
            for( v = 0; v < nvars + 1; ++v )
            {
               assert(scdata.vars[v] != NULL || v == nvars);
               SCIPdebugMsg(scip,  "\n%s %g %g", SCIPvarGetName(scdata.vars[v]), scdata.lbs[v], scdata.ubs[v]);
            }
#endif

            SCIP_CALL( SCIPisConsRedundantWithinBounds(scip, conshdlr, cons, &cutoff, &redundant, &scdata, &activity, childactivities) );

            if( redundant )
            {
               int nlockspos;
               int nlocksneg;
               SCIP_Real hasrhs;
               SCIP_Real haslhs;

               hasrhs = !SCIPisInfinity(scip, SCIPgetRhsConsExpr(scip, cons));
               haslhs = !SCIPisInfinity(scip, -SCIPgetLhsConsExpr(scip, cons));

               if( (SCIPgetConsExprExprHdlr(expr) != SCIPgetConsExprExprHdlrSum(conshdlr)) || hasnondefault )
               {
                  SCIP_Bool bigm = FALSE;

                  nlockspos = SCIPgetConsExprExprNLocksPos(expr);
                  nlocksneg = SCIPgetConsExprExprNLocksNeg(expr);

                  /* check locks, mark expr as bigm with this indicator */
                  if( nlockspos == 1 && nlocksneg == 0 )
                  {
                     if( hasrhs && !haslhs )
                     {
//                        SCIPinfoMessage(scip, NULL, "\nexpr %p can be set to its max when indicator %s is 0\n",
//                                        expr, SCIPvarGetName(indicators[i]));
                        bigm = TRUE;
                        SCIPsetConsExprExprBigMMax(expr, TRUE);
                     }

                     else if( !hasrhs && haslhs )
                     {
//                        SCIPinfoMessage(scip, NULL, "\nexpr %p can be set to its min when indicator %s is 0\n",
//                                        expr, SCIPvarGetName(indicators[i]));
                        bigm = TRUE;
                        SCIPsetConsExprExprBigMMax(expr, FALSE);
                     }
                  }

                  if( nlocksneg == 1 && nlockspos == 0 )
                  {
                     if( !hasrhs && haslhs )
                     {
//                        SCIPinfoMessage(scip, NULL, "\nexpr %p can be set to its max when indicator %s is 0\n",
//                                        expr, SCIPvarGetName(indicators[i]));
                        bigm = TRUE;
                        SCIPsetConsExprExprBigMMax(expr, TRUE);
                     }
                     else if( hasrhs && !haslhs )
                     {
//                        SCIPinfoMessage(scip, NULL, "\nexpr %p can be set to its min when indicator %s is 0\n",
//                                        expr, SCIPvarGetName(indicators[i]));
                        bigm = TRUE;
                        SCIPsetConsExprExprBigMMax(expr, FALSE);
                     }
                  }
                  if( bigm )
                  {
                     SCIPsetConsExprExprBigM(expr, TRUE);
                     SCIP_CALL( SCIPaddConsExprExprBigMIndicator(scip, expr, indicators[i], activity) );
                  }
               }
               else
               {
                  /* if a sum expression has only the default nlhdlr, mark its children as bigm instead */
                  for( c = 0; c < SCIPgetConsExprExprNChildren(expr); ++c )
                  {
                     SCIP_Bool bigm = FALSE;
                     SCIP_CONSEXPR_EXPR* child;

                     child = SCIPgetConsExprExprChildren(expr)[c];
                     nlockspos = SCIPgetConsExprExprNLocksPos(child);
                     nlocksneg = SCIPgetConsExprExprNLocksNeg(child);

                     if( nlockspos == 1 && nlocksneg == 0 )
                     {
                        if( hasrhs && !haslhs )
                        {
//                           SCIPinfoMessage(scip, NULL, "\nchild %p can be set to its max when indicator %s is 0\n",
//                                           child, SCIPvarGetName(indicators[i]));
                           bigm = TRUE;
                           SCIPsetConsExprExprBigMMax(child, TRUE);
                        }

                        else if( !hasrhs && haslhs )
                        {
//                           SCIPinfoMessage(scip, NULL, "\nchild %p can be set to its min when indicator %s is 0\n",
//                                           child, SCIPvarGetName(indicators[i]));
                           bigm = TRUE;
                           SCIPsetConsExprExprBigMMax(child, FALSE);
                        }

                     }
                     else if( nlocksneg == 1 && nlockspos == 0 )
                     {
                        if( !hasrhs && haslhs )
                        {
//                           SCIPinfoMessage(scip, NULL, "\nchild %p can be set to its max when indicator %s is 0\n",
//                                           child, SCIPvarGetName(indicators[i]));
                           bigm = TRUE;
                           SCIPsetConsExprExprBigMMax(child, TRUE);
                        }
                        else if( hasrhs && !haslhs )
                        {
//                           SCIPinfoMessage(scip, NULL, "\nchild %p can be set to its min when indicator %s is 0\n",
//                                           child, SCIPvarGetName(indicators[i]));
                           bigm = TRUE;
                           SCIPsetConsExprExprBigMMax(child, FALSE);
                        }
                     }
                     if( bigm )
                     {
                        SCIPsetConsExprExprBigM(child, TRUE);
                        SCIP_CALL( SCIPaddConsExprExprBigMIndicator(scip, child, indicators[i], childactivities[c]) );
                     }
                  }
               }
            }
         }
      }
      if( SCIPgetConsExprExprHdlr(expr) == SCIPgetConsExprExprHdlrSum(conshdlr) )
      {
         SCIPfreeBufferArray(scip, &childactivities);
      }
      SCIPfreeBufferArray(scip, &scdata.vars);
      SCIPfreeBufferArray(scip, &scdata.lbs);
      SCIPfreeBufferArray(scip, &scdata.ubs);
   }

   SCIPfreeBlockMemoryArrayNull(scip, &indicators, indicatorssize);
   SCIPfreeBufferArray(scip, &scvdatas);

   return SCIP_OKAY;
}

/** callback to detect structure in expression tree
 *
 *  We are looking for expressions g(x), where x is a vector of semicontinuous variables that all share at least one
 *  indicator variable.
 */
static
SCIP_DECL_CONSEXPR_NLHDLRDETECT(nlhdlrDetectPerspective)
{ /*lint --e{715}*/
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;
   SCIP_CONSEXPR_EXPR** varexprs;
   SCIP_Bool success = FALSE;
   int i;
   SCIP_Bool hassepabelow = FALSE;
   SCIP_Bool hassepaabove = FALSE;
   SCIP_Bool hasnondefault = FALSE;
   /* some variables for statistics */
   SCIP_Bool hasconvexsepa = FALSE;
   SCIP_Bool hasnonconvexsepa = FALSE;
   SCIP_Bool bigm = FALSE;
   int nvars;
   SCIP_VAR** vars;

   nlhdlrdata = SCIPgetConsExprNlhdlrData(nlhdlr);

   assert(scip != NULL);
   assert(nlhdlr != NULL);
   assert(expr != NULL);
   assert(participating != NULL);
   assert(enforcing != NULL);
   assert(nlhdlrexprdata != NULL);
   assert(nlhdlrdata != NULL);

   /* do not run if we will have no auxvar to add a cut for */
   if( SCIPgetConsExprExprNAuxvarUses(expr) == 0 )
      return SCIP_OKAY;

   if( SCIPgetNBinVars(scip) == 0 )
   {
      SCIPdebugMsg(scip, "problem has no binary variables, not running perspective detection\n");
      return SCIP_OKAY;
   }

   for( i = 0; i < SCIPgetConsExprExprNEnfos(expr); ++i )
   {
      SCIP_CONSEXPR_NLHDLR* nlhdlr2;
      SCIP_CONSEXPR_EXPRENFO_METHOD nlhdlr2participates;
      SCIP_Bool sepabelowusesactivity;
      SCIP_Bool sepaaboveusesactivity;
      SCIPgetConsExprExprEnfoData(expr, i, &nlhdlr2, NULL, &nlhdlr2participates, &sepabelowusesactivity, &sepaaboveusesactivity, NULL);

      if( (nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPABOTH) == 0 )
         continue;

      if( !SCIPhasConsExprNlhdlrEstimate(nlhdlr2) )
         continue;

      if( strcmp(SCIPgetConsExprNlhdlrName(nlhdlr2), "default") != 0 )
         hasnondefault = TRUE;

      /* If we are supposed to run only on convex expressions, than check whether there is a nlhdlr
       * that participates in separation without using activity for it. Otherwise, check for
       * participation regardless of activity usage.
       */
      if( (nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPABELOW) && (!nlhdlrdata->convexonly || !sepabelowusesactivity) )
         hassepabelow = TRUE;

      if( (nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPAABOVE) && (!nlhdlrdata->convexonly || !sepaaboveusesactivity) )
         hassepaabove = TRUE;

      /* save more sepa information for statistics */
      if( ((nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPABELOW) && !sepabelowusesactivity) ||
          ((nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPAABOVE) && !sepaaboveusesactivity) )
         hasconvexsepa = TRUE;

      if( ((nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPABELOW) && sepabelowusesactivity) ||
          ((nlhdlr2participates & SCIP_CONSEXPR_EXPRENFO_SEPAABOVE) && sepaaboveusesactivity) )
         hasnonconvexsepa = TRUE;
   }

   /* get varexprs */
   SCIP_CALL( SCIPgetConsExprExprNVars(scip, conshdlr, expr, &nvars) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &vars, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &varexprs, nvars) );
   SCIP_CALL( SCIPgetConsExprExprVarExprs(scip, conshdlr, expr, varexprs, &nvars) );
   for( i = 0; i < nvars; ++i )
   {
      vars[i] = SCIPgetConsExprExprVarVar(varexprs[i]);
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &varexprs[i]) );
      SCIP_CALL( SCIPcaptureVar(scip, vars[i]) );
   }
   SCIPsortPtr((void**) vars, SCIPvarComp, nvars);
   SCIPfreeBufferArray(scip, &varexprs);

   if( nlhdlrdata->scvars == NULL )
   {
      SCIP_CALL( SCIPhashmapCreate(&(nlhdlrdata->scvars), SCIPblkmem(scip), SCIPgetNVars(scip)) );
   }

   if( cons != NULL && SCIPgetSubscipDepth(scip) == 0 && nlhdlrdata->bigmcuts )
   {
      SCIP_CALL( consIsBigM(scip, conshdlr, nlhdlr, cons, expr, vars, nvars, hasnondefault, &bigm) );
   }

   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL( SCIPreleaseVar(scip, &vars[i]) );
   }
   SCIPfreeBlockMemoryArray(scip, &vars, nvars);

   /* If no other nlhdlr separates, neither does perspective (if convexonly, only separation
    * without using activity counts)
    */
   if( !hassepabelow && !hassepaabove )
   {
      SCIPdebugMsg(scip, "no nlhdlr separates without using activity, not running perspective detection\n");
      return SCIP_OKAY;
   }

   /* If a sum expression is handled only by default nlhdlr, then all the children will have auxiliary vars.
    * Since the sum will then be linear in auxiliary variables, perspective can't improve anything for it
    */
   if( SCIPgetConsExprExprHdlr(expr) == SCIPgetConsExprExprHdlrSum(conshdlr) && !hasnondefault )
   {
      SCIPdebugMsg(scip, "sum expr only has default exprhdlr, not running perspective detection\n");
      return SCIP_OKAY;
   }

#ifdef SCIP_DEBUG
   SCIPdebugMsg(scip, "Called perspective detect, expr = %p: ", expr);
   SCIPprintConsExprExpr(scip, conshdlr, expr, NULL);
   SCIPdebugMsgPrint(scip, "\n");
#endif

   /* allocate memory */
   SCIP_CALL( SCIPallocClearBlockMemory(scip, nlhdlrexprdata) );
#ifdef SCIP_DISABLED_CODE
   /* move this up for the purposes of consIsBigM */
   if( nlhdlrdata->scvars == NULL )
   {
      SCIP_CALL( SCIPhashmapCreate(&(nlhdlrdata->scvars), SCIPblkmem(scip), SCIPgetNVars(scip)) );
   }
#endif

   /* save varexprs to nlhdlrexprdata */
   SCIP_CALL( SCIPgetConsExprExprNVars(scip, conshdlr, expr, &(*nlhdlrexprdata)->nvars) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(*nlhdlrexprdata)->vars, (*nlhdlrexprdata)->nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &varexprs, (*nlhdlrexprdata)->nvars) );
   (*nlhdlrexprdata)->varssize = (*nlhdlrexprdata)->nvars;
   SCIP_CALL( SCIPgetConsExprExprVarExprs(scip, conshdlr, expr, varexprs, &(*nlhdlrexprdata)->nvars) );
   for( i = 0; i < (*nlhdlrexprdata)->nvars; ++i )
   {
      (*nlhdlrexprdata)->vars[i] = SCIPgetConsExprExprVarVar(varexprs[i]);
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &varexprs[i]) );
      SCIP_CALL( SCIPcaptureVar(scip, (*nlhdlrexprdata)->vars[i]) );
   }
   SCIPsortPtr((void**) (*nlhdlrexprdata)->vars, SCIPvarComp, (*nlhdlrexprdata)->nvars);
   SCIPfreeBufferArray(scip, &varexprs);

   /* check if expr is semicontinuous and save indicator variables */
   SCIP_CALL( exprIsSemicontinuous(scip, conshdlr, nlhdlrdata, *nlhdlrexprdata, expr, &success) );

   if( !success && SCIPgetConsExprExprBigM(expr) )
   {
      /* expr is not semicontinuous, but can be treated as such due to participating in a big-M constraint */
      ++(nlhdlrdata->nonlybigmdetects);
      success = TRUE;
      (*nlhdlrexprdata)->onlybigm = TRUE;
   }

   if( success )
   {
      assert(*nlhdlrexprdata != NULL);
      assert((*nlhdlrexprdata)->nindicators > 0 || SCIPgetConsExprExprBigM(expr));

      if( hassepaabove )
         *participating |= SCIP_CONSEXPR_EXPRENFO_SEPAABOVE;
      if( hassepabelow )
         *participating |= SCIP_CONSEXPR_EXPRENFO_SEPABELOW;

      ++(nlhdlrdata->ndetects);

      if( hasconvexsepa )
         ++(nlhdlrdata->nconvexdetects);
      if( hasnonconvexsepa && !nlhdlrdata->convexonly )
         ++(nlhdlrdata->nnonconvexdetects);

#ifdef SCIP_DEBUG
      SCIPinfoMessage(scip, NULL, "detected an on/off expr: ");
      SCIPprintConsExprExpr(scip, conshdlr, expr, NULL);
      SCIPinfoMessage(scip, NULL, "\n");
#endif
   }
   else if( *nlhdlrexprdata != NULL )
   {
      SCIP_CALL( nlhdlrFreeExprDataPerspective(scip, nlhdlr, expr, nlhdlrexprdata) );
   }

   return SCIP_OKAY;
}


/** auxiliary evaluation callback of nonlinear handler */
static
SCIP_DECL_CONSEXPR_NLHDLREVALAUX(nlhdlrEvalauxPerspective)
{ /*lint --e{715}*/
   int e;
   SCIP_Real maxdiff;
   SCIP_Real auxvarvalue;
   SCIP_Real enfoauxval;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(auxvalue != NULL);

   auxvarvalue = SCIPgetSolVal(scip, sol, SCIPgetConsExprExprAuxVar(expr));
   maxdiff = 0.0;
   *auxvalue = auxvarvalue;

   /* use the auxvalue from one of the other nlhdlrs that estimates for this expr: take the one that is farthest
    * from the current value of auxvar
    */
   for( e = 0; e < SCIPgetConsExprExprNEnfos(expr); ++e )
   {
      SCIP_CONSEXPR_NLHDLR* nlhdlr2;
      SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlr2exprdata;
      SCIP_CONSEXPR_EXPRENFO_METHOD nlhdlr2participation;

      SCIPgetConsExprExprEnfoData(expr, e, &nlhdlr2, &nlhdlr2exprdata, &nlhdlr2participation, NULL, NULL, NULL);

      /* skip nlhdlr that do not participate or do not provide estimate */
      if( (nlhdlr2participation & SCIP_CONSEXPR_EXPRENFO_SEPABOTH) == 0 || !SCIPhasConsExprNlhdlrEstimate(nlhdlr2) )
         continue;

      SCIP_CALL( SCIPevalauxConsExprNlhdlr(scip, nlhdlr2, expr, nlhdlr2exprdata, &enfoauxval, sol) );

      SCIPsetConsExprExprEnfoAuxValue(expr, e, enfoauxval);

      if( REALABS(enfoauxval - auxvarvalue) > maxdiff && enfoauxval != SCIP_INVALID ) /*lint !e777*/
      {
         maxdiff = REALABS(enfoauxval - auxvarvalue);
         *auxvalue = enfoauxval;
      }
   }

   return SCIP_OKAY;
}

/** separation initialization method of a nonlinear handler */
static
SCIP_DECL_CONSEXPR_NLHDLRINITSEPA(nlhdlrInitSepaPerspective)
{ /*lint --e{715}*/
   int sindicators;
   SCIP_CONSEXPR_EXPR* curexpr;
   SCIP_VAR* var;
   int i;
   SCIP_CONSEXPR_BNDDATA scdata;
   int nvars;
   SCIP_VAR** vars;
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;
   SCIP_VAR** scauxvars;
   int nscauxvars;
   SCIP_Real* auxvals0;

   sindicators = nlhdlrexprdata->nindicators;
   nlhdlrdata = SCIPgetConsExprNlhdlrData(nlhdlr);

   /* save auxiliary variables */
   SCIP_CALL( saveAuxVars(scip, conshdlr, SCIPgetConsExprNlhdlrData(nlhdlr), nlhdlrexprdata, expr) );

   /* compute 'off' values of expr and subexprs (and thus auxvars too) */
   SCIP_CALL( computeOffValues(scip, conshdlr, SCIPgetConsExprNlhdlrData(nlhdlr), nlhdlrexprdata, expr) );

   nvars = nlhdlrexprdata->nvars;
   vars = nlhdlrexprdata->vars;

   SCIP_CALL( SCIPallocBufferArray(scip, &scdata.vars, nvars + 1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &scdata.lbs, nvars + 1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &scdata.ubs, nvars + 1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &scauxvars, nvars + 1) );
   SCIP_CALL( SCIPallocBufferArray(scip, &auxvals0, nvars + 1) );

   /* for each indicator, do propagation at 0 to add any remaining semi-continuous auxvars */
   for( i = 0; i < SCIPgetConsExprExprNBigMIndicators(expr); ++i )
   {
      SCIP_Bool indincons = FALSE; /* whether indicator is one of the cons vars */
      int v;
      SCIP_VAR* indicator = SCIPgetConsExprExprBigMIndicators(expr)[i];
      SCVARDATA* scvdata;
      SCIP_Bool cutoff;

      /* fill in scvdatas and intevalvardata */
      for( v = 0; v < nvars; ++v )
      {
         SCIP_Bool found = FALSE;
         int pos;

         scdata.vars[v] = vars[v];
         scvdata = (SCVARDATA *) SCIPhashmapGetImage(nlhdlrdata->scvars, (void *) vars[v]);
         if( scvdata != NULL )
         {
            found = SCIPsortedvecFindPtr((void **) scvdata->bvars, SCIPvarComp, (void *) indicator,
                                         scvdata->nbnds, &pos);
         }

         if( found )
         {
            scdata.lbs[v] = scvdata->vals0[pos];
            scdata.ubs[v] = scvdata->vals0[pos];
         }
         else if( vars[v] == indicator )
         {
            indincons = TRUE;
            scdata.lbs[v] = 0.0;
            scdata.ubs[v] = 0.0;
         }
         else
         {
            scdata.lbs[v] = SCIPvarGetLbGlobal(vars[v]);
            scdata.ubs[v] = SCIPvarGetUbGlobal(vars[v]);
         }
      }

      if( !indincons )
      {
         scdata.vars[nvars] = indicator;
         scdata.lbs[nvars] = 0.0;
         scdata.ubs[nvars] = 0.0;
         scdata.nvars = nvars+1;
      }
      else
      {
         scdata.vars[nvars] = NULL;
         scdata.nvars = nvars;
      }

      nscauxvars = 0;

      /* evaluate expression activity at 0 and get bounds on auxiliary variables */
      SCIP_CALL( SCIPcomputeZeroAuxValues(scip, conshdlr, expr, &cutoff, &scdata, scauxvars, &nscauxvars, auxvals0) );

      /* save any newly detected semicontinuous variables */
      for( v = 0; v < nscauxvars; ++v )
      {
         scvdata = (SCVARDATA *) SCIPhashmapGetImage(nlhdlrdata->scvars, (void *) scauxvars[v]);
         if( scvdata == NULL )
         {
            SCIP_CALL( SCIPallocClearBlockMemory(scip, &scvdata) );
            SCIP_CALL( SCIPhashmapInsert(nlhdlrdata->scvars, scauxvars[v], scvdata) );
         }

         SCIP_CALL( addSCVarIndicator(scip, scvdata, indicator, auxvals0[v], SCIPvarGetLbGlobal(scauxvars[v]), SCIPvarGetUbGlobal(scauxvars[v])) );
      }
   } /* indicator */

   /* some indicator variables might have been removed if evaluation failed, check how many remain */
   if( nlhdlrexprdata->nindicators == 0 )
   {
      SCIPfreeBlockMemoryArray(scip, &nlhdlrexprdata->indicators, sindicators);
      SCIPfreeBlockMemoryArray(scip, &nlhdlrexprdata->exprvals0, sindicators);
   }
   else if( nlhdlrexprdata->nindicators < sindicators )
   {
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &nlhdlrexprdata->indicators, sindicators, nlhdlrexprdata->nindicators) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &nlhdlrexprdata->exprvals0, sindicators, nlhdlrexprdata->nindicators) );
   }

   SCIPfreeBufferArray(scip, &auxvals0);
   SCIPfreeBufferArray(scip, &scauxvars);
   SCIPfreeBufferArray(scip, &scdata.ubs);
   SCIPfreeBufferArray(scip, &scdata.lbs);
   SCIPfreeBufferArray(scip, &scdata.vars);

   return SCIP_OKAY;
}


/** separation deinitialization method of a nonlinear handler (called during CONSEXITSOL) */
#if 0
static
SCIP_DECL_CONSEXPR_NLHDLREXITSEPA(nlhdlrExitSepaPerspective)
{ /*lint --e{715}*/
   SCIPerrorMessage("method of perspective nonlinear handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#endif

static
SCIP_RETCODE computeIndicatorCoef(
   SCIP*                 scip,
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata,
   SCIP_ROWPREP*         rowprep,
   SCIP_VAR*             auxvar,
   SCIP_VAR*             indicator,
   SCIP_Bool             max,
   SCIP_Real*            cst0,
   SCIP_INTERVAL         activity
   )
{
   SCIP_Bool allfixed;
   int i;
   int j;
   SCIP_VAR* var;
   SCIP_Real cutval_min;
   SCIP_Real cutval_max;

   *cst0 = SCIP_INVALID;

   /* assemble box */
   allfixed = TRUE;
   cutval_min = -rowprep->side;
   cutval_max = -rowprep->side;
   for( i = 0; i < rowprep->nvars; ++i )
   {
      SCVARDATA* scvdata;
      SCIP_Bool found = FALSE;
      SCIP_Real vlb;
      SCIP_Real vub;
      int pos;

      var = rowprep->vars[i];
      assert(var != NULL);
      scvdata = (SCVARDATA*) SCIPhashmapGetImage(nlhdlrdata->scvars, (void*)var);

      if( scvdata != NULL )
      {
         found = SCIPsortedvecFindPtr((void **) scvdata->bvars, SCIPvarComp, (void *) indicator, scvdata->nbnds, &pos);
      }

      if( found )
      {
         vlb = scvdata->vals0[pos];
         vub = scvdata->vals0[pos];
      }
      else if( var == indicator )
      {
         vlb = 0.0;
         vub = 0.0;
      }
      else
      {
         vlb = SCIPvarGetLbLocal(var);
         vub = SCIPvarGetUbLocal(var);
      }

      if( var == auxvar )
      {
         vlb = MAX(vlb, SCIPintervalGetInf(activity));
         vub = MAX(vub, SCIPintervalGetSup(activity));
      }

      SCIPdebugMsg(scip, "\nbounds on %s (index %d) at 0 are [%g,%g]", SCIPvarGetName(var), i, vlb, vub);

      if( SCIPisInfinity(scip, vub) || SCIPisInfinity(scip, -vlb) )
      {
         SCIPdebugMsg(scip, "bound at infinity, no bigm cut strengthening possible\n");
         return SCIP_OKAY;
      }

      if( !SCIPisRelEQ(scip, vlb, vub) )
         allfixed = FALSE;

      if( rowprep->coefs[i] > 0 )
      {
         cutval_min += rowprep->coefs[i] * vlb;
         cutval_max += rowprep->coefs[i] * vub;
      }
      else
      {
         cutval_min += rowprep->coefs[i] * vub;
         cutval_max += rowprep->coefs[i] * vlb;
      }
   }

   if( allfixed )
   {
      SCIPdebugMsg(scip, "all variables fixed, skip big-M perspectify (it should have been handled as semi-continuous)\n");
      return SCIP_OKAY;
   }

   *cst0 = rowprep->sidetype == SCIP_SIDETYPE_RIGHT ? -cutval_max : -cutval_min;
   SCIPdebugMsg(scip, "\ncst0 = %g", *cst0);

   return SCIP_OKAY;
}

/** nonlinear handler enforcement callback
 *
 * "Perspectivies" cuts produced by other handlers. Suppose that we want to separate x from the set g(x) <= 0.
 * If g(x) = g0 if indicator z = 0, and a cut is given by sum aixi + c <= aux, where xi = xi0 if z = 0 for all i,
 * then the "perspectivied" cut is sum aixi + c + (1 - z)*(g0 - c - sum aix0i) <= aux. This ensures that at z = 1,
 * the new cut is equivalent to the given cut, and at z = 0 it reduces to g0 <= aux.
 */
static
SCIP_DECL_CONSEXPR_NLHDLRENFO(nlhdlrEnfoPerspective)
{ /*lint --e{715}*/
   SCIP_ROWPREP* rowprep;
   SCIP_VAR* auxvar;
   int i;
   int j;
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;
   SCIP_Real cst0;
   SCIP_VAR* indicator;
   SCIP_PTRARRAY* rowpreps2;
   SCIP_PTRARRAY* rowpreps;
   int nrowpreps;
   SCIP_SOL* solcopy;
   SCIP_Bool doprobing;
   SCIP_BOOLARRAY* addedbranchscores2;
   SCIP_Bool stop;
   int nenfos;
   int* enfoposs;
   SCIP_SOL* soladj;
   int pos;
   SCVARDATA* scvdata;

   nlhdlrdata = SCIPgetConsExprNlhdlrData(nlhdlr);

#ifdef SCIP_DEBUG
   SCIPinfoMessage(scip, NULL, "enforcement method of perspective nonlinear handler called for expr %p: ", expr);
   SCIP_CALL( SCIPprintConsExprExpr(scip, conshdlr, expr, NULL) );
   SCIPinfoMessage(scip, NULL, " at\n");
   for( i = 0; i < nlhdlrexprdata->nvars; ++i )
   {
      SCIPinfoMessage(scip, NULL, "%s = %g\n", SCIPvarGetName(nlhdlrexprdata->vars[i]),
              SCIPgetSolVal(scip, sol, nlhdlrexprdata->vars[i]));
   }
   SCIPinfoMessage(scip, NULL, "%s = %g", SCIPvarGetName(SCIPgetConsExprExprAuxVar(expr)),
           SCIPgetSolVal(scip, sol, SCIPgetConsExprExprAuxVar(expr)));
#endif

   assert(scip != NULL);
   assert(expr != NULL);
   assert(conshdlr != NULL);
   assert(nlhdlrexprdata != NULL);
   assert(nlhdlrdata != NULL);

   auxvar = SCIPgetConsExprExprAuxVar(expr);
   assert(auxvar != NULL);

   /* detect should have picked only those expressions for which at least one other nlhdlr can enforce */
   assert(SCIPgetConsExprExprNEnfos(expr) > 1);

   SCIP_CALL( SCIPallocBufferArray(scip, &enfoposs, SCIPgetConsExprExprNEnfos(expr) - 1) );

   doprobing = FALSE;
   nenfos = 0;

   /* find suitable nlhdlrs and check if there is enough violation to do probing */
   for( j = 0; j < SCIPgetConsExprExprNEnfos(expr); ++j )
   {
      SCIP_CONSEXPR_NLHDLR* nlhdlr2;
      SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlr2exprdata;
      SCIP_CONSEXPR_EXPRENFO_METHOD nlhdlr2participate;
      SCIP_Real nlhdlr2auxvalue;
      SCIP_Real violation;
      SCIP_Bool violbelow;
      SCIP_Bool violabove;
      SCIP_Bool sepausesactivity = FALSE;

      SCIPgetConsExprExprEnfoData(expr, j, &nlhdlr2, &nlhdlr2exprdata, &nlhdlr2participate, !overestimate ? &sepausesactivity : NULL, overestimate ? &sepausesactivity: NULL, &nlhdlr2auxvalue);  /*lint !e826*/

      if( nlhdlr2 == nlhdlr )
         continue;

      /* if nlhdlr2 cannot estimate, then cannot use it */
      if( !SCIPhasConsExprNlhdlrEstimate(nlhdlr2) )
         continue;

      /* if nlhdlr2 does not participate in the separation on the desired side (overestimate), then skip it */
      if( (nlhdlr2participate & (overestimate ? SCIP_CONSEXPR_EXPRENFO_SEPAABOVE : SCIP_CONSEXPR_EXPRENFO_SEPABELOW)) == 0 )
         continue;

      /* if only working on convex-looking expressions, then skip nlhdlr if it uses activity for estimates */
      if( nlhdlrdata->convexonly && sepausesactivity )
         continue;

      /* evalaux should have called evalaux of nlhdlr2 by now
       * check whether handling the violation for nlhdlr2 requires under- or overestimation and this fits to overestimate flag
       */
      SCIP_CALL( SCIPgetConsExprExprAbsAuxViolation(scip, conshdlr, expr, nlhdlr2auxvalue,
            sol, &violation, &violbelow, &violabove) );
      assert(violation >= 0.0);

      if( (overestimate && !violabove) || (!overestimate && !violbelow) )
         continue;

      /* if violation is small, cuts would likely be weak - skip perspectification */
      if( !allowweakcuts && violation < SCIPfeastol(scip) )
         continue;

      enfoposs[nenfos] = j;
      ++nenfos;

      /* enable probing if tightening the domain could be useful for nlhdlr and violation is above threshold */
      if( sepausesactivity && violation >= nlhdlrdata->minviolprobing )
         doprobing = TRUE;
   }

   if( nenfos == 0 )
   {
      *result = SCIP_DIDNOTRUN;
      SCIPfreeBufferArray(scip, &enfoposs);
      return SCIP_OKAY;
   }

   /* check probing frequency against depth in b&b tree */
   if( nlhdlrdata->probingfreq == -1 || (nlhdlrdata->probingfreq == 0 && SCIPgetDepth(scip) != 0) ||
      (nlhdlrdata->probingfreq > 0 && SCIPgetDepth(scip) % nlhdlrdata->probingfreq != 0)  )
      doprobing = FALSE;

   /* if addbranchscores is TRUE, then we can assume to be in enforcement and not in separation */
   if( nlhdlrdata->probingonlyinsepa && addbranchscores )
      doprobing = FALSE;

   /* disable probing if already being in probing or if in a subscip */
   if( SCIPinProbing(scip) || SCIPgetSubscipDepth(scip) != 0 )
      doprobing = FALSE;

   nrowpreps = 0;
   *result = SCIP_DIDNOTFIND;
   solcopy = sol;
   stop = FALSE;

   SCIP_CALL( SCIPcreatePtrarray(scip, &rowpreps2) );
   SCIP_CALL( SCIPcreatePtrarray(scip, &rowpreps) );
   SCIP_CALL( SCIPcreateBoolarray(scip, &addedbranchscores2) );

   /* build cuts for every indicator variable */
   for( i = 0; i < nlhdlrexprdata->nindicators && !stop; ++i )
   {
      int v;
      int minidx;
      int maxidx;
      int r;
      SCIP_VAR** probingvars;
      SCIP_INTERVAL* probingdoms;
      int nprobingvars;
      SCIP_Bool doprobingind;
      SCIP_Real indval;

      indicator = nlhdlrexprdata->indicators[i];
      probingvars = NULL;
      probingdoms = NULL;
      nprobingvars = 0;
      doprobingind = doprobing;

      SCIP_CALL( analyseOnoffBounds(scip, nlhdlrdata, nlhdlrexprdata, indicator, &probingvars, &probingdoms,
            &nprobingvars, &doprobingind, result) );

      /* don't add perspective cuts for fixed indicators since there is no use for perspectivy */
      if( SCIPvarGetLbLocal(indicator) >= 0.5 )
      {
         assert(!doprobingind);
         continue;
      }
      if( SCIPvarGetUbLocal(indicator) <= 0.5 )
      { /* this case is stronger as it implies that everything is fixed;
         * therefore we are now happy
         */
         assert(!doprobingind);
         goto TERMINATE;
      }

      if( doprobingind )
      {
         SCIP_Bool propagate;
         SCIP_Bool cutoff_probing;
         SCIP_Bool cutoff;
         SCIP_Bool fixed;

#ifndef NDEBUG
         SCIP_Real* solvals;
         SCIP_CALL( SCIPallocBufferArray(scip, &solvals, nlhdlrexprdata->nvars) );
         for( v = 0; v < nlhdlrexprdata->nvars; ++v )
         {
            solvals[v] = SCIPgetSolVal(scip, sol, nlhdlrexprdata->vars[v]);
         }
#endif

         propagate = SCIPgetDepth(scip) == 0;

         SCIP_CALL( startProbing(scip, nlhdlrdata, nlhdlrexprdata, indicator, probingvars, probingdoms, nprobingvars,
               sol, &solcopy, &cutoff_probing) );

#ifndef NDEBUG
         for( v = 0; v < nlhdlrexprdata->nvars; ++v )
         {
            assert(solvals[v] == SCIPgetSolVal(scip, solcopy, nlhdlrexprdata->vars[v])); /*lint !e777*/
         }
         SCIPfreeBufferArray(scip, &solvals);
#endif

         if( propagate )
         { /* we are in the root node and startProbing did propagation */
            /* probing propagation might have detected infeasibility */
            if( cutoff_probing )
            {
               /* indicator == 1 is infeasible -> set indicator to 0 */
               SCIPfreeBufferArrayNull(scip, &probingvars);
               SCIPfreeBufferArrayNull(scip, &probingdoms);

               SCIP_CALL( SCIPendProbing(scip) );

               SCIP_CALL( SCIPfixVar(scip, indicator, 0.0, &cutoff, &fixed) );

               if( cutoff )
               {
                  *result = SCIP_CUTOFF;
                  goto TERMINATE;
               }

               continue;
            }

            /* probing propagation in the root node can provide better on/off bounds */
            SCIP_CALL( tightenOnBounds(nlhdlrexprdata, nlhdlrdata->scvars, indicator) );
         }
      }

      if( nlhdlrdata->adjrefpoint )
      {
         /* make sure that when we adjust the point, we don't divide by something too close to 0.0 */
         indval = MAX(SCIPgetSolVal(scip, solcopy, indicator), 0.1);

         /* create an adjusted point x^adj = (x* - x0) / z* + x0 */
         SCIP_CALL( SCIPcreateSol(scip, &soladj, NULL) );
         for( v = 0; v < nlhdlrexprdata->nvars; ++v )
         {
            if( SCIPvarGetStatus(nlhdlrexprdata->vars[v]) == SCIP_VARSTATUS_FIXED )
               continue;

            scvdata = getSCVarDataInd(nlhdlrdata->scvars, nlhdlrexprdata->vars[v], indicator, &pos);

            /* a non-semicontinuous variable must be linear in expr; skip it */
            if( scvdata == NULL )
               continue;

            SCIP_CALL( SCIPsetSolVal(scip, soladj, nlhdlrexprdata->vars[v],
                  (SCIPgetSolVal(scip, solcopy, nlhdlrexprdata->vars[v]) - scvdata->vals0[pos]) / indval
                  + scvdata->vals0[pos]) );
         }
         for( v = 0; v < nlhdlrexprdata->nindicators; ++v )
         {
            if( SCIPvarGetStatus(nlhdlrexprdata->indicators[v]) == SCIP_VARSTATUS_FIXED )
               continue;

            SCIP_CALL( SCIPsetSolVal(scip, soladj, nlhdlrexprdata->indicators[v],
                  SCIPgetSolVal(scip, solcopy, nlhdlrexprdata->indicators[v])) );
         }
         if( SCIPvarGetStatus(auxvar) != SCIP_VARSTATUS_FIXED )
            SCIP_CALL( SCIPsetSolVal(scip, soladj, auxvar, SCIPgetSolVal(scip, solcopy, auxvar)) );
      }

      /* use cuts from every suitable nlhdlr */
      for( j = 0; j < nenfos; ++j )
      {
         SCIP_Bool addedbranchscores2j;
         SCIP_CONSEXPR_NLHDLR* nlhdlr2;
         SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlr2exprdata;
         SCIP_Real nlhdlr2auxvalue;
         SCIP_Bool success2;

         SCIPgetConsExprExprEnfoData(expr, enfoposs[j], &nlhdlr2, &nlhdlr2exprdata, NULL, NULL, NULL, &nlhdlr2auxvalue);
         assert(SCIPhasConsExprNlhdlrEstimate(nlhdlr2) && nlhdlr2 != nlhdlr);

         if( nlhdlrdata->adjrefpoint )
         {
            SCIP_CALL( SCIPevalauxConsExprNlhdlr(scip, nlhdlr2, expr, nlhdlr2exprdata, &nlhdlr2auxvalue, soladj) );
            SCIPsetConsExprExprEnfoAuxValue(expr, j, nlhdlr2auxvalue);
         }

         SCIPdebugMsg(scip, "asking nonlinear handler %s to %sestimate\n", SCIPgetConsExprNlhdlrName(nlhdlr2), overestimate ? "over" : "under");

         /* ask the nonlinear handler for an estimator */
         if( nlhdlrdata->adjrefpoint )
         {
            SCIP_CALL( SCIPestimateConsExprNlhdlr(scip, conshdlr, nlhdlr2, expr,
                  nlhdlr2exprdata, soladj,
                  nlhdlr2auxvalue, overestimate, SCIPgetSolVal(scip, solcopy, auxvar),
                  rowpreps2, &success2, addbranchscores, &addedbranchscores2j) );
         }
         else
         {
            SCIP_CALL( SCIPestimateConsExprNlhdlr(scip, conshdlr, nlhdlr2, expr,
                  nlhdlr2exprdata, solcopy,
                  nlhdlr2auxvalue, overestimate, SCIPgetSolVal(scip, solcopy, auxvar),
                  rowpreps2, &success2, addbranchscores, &addedbranchscores2j) );
         }

         minidx = SCIPgetPtrarrayMinIdx(scip, rowpreps2);
         maxidx = SCIPgetPtrarrayMaxIdx(scip, rowpreps2);

         assert((success2 && minidx <= maxidx) || (!success2 && minidx > maxidx));

         /* perspectivy all cuts from nlhdlr2 and add them to rowpreps */
         for( r = minidx; r <= maxidx; ++r )
         {
            SCIP_Real maxcoef;

            rowprep = (SCIP_ROWPREP*) SCIPgetPtrarrayVal(scip, rowpreps2, r);
            assert(rowprep != NULL);

#ifdef SCIP_DEBUG
            SCIPinfoMessage(scip, NULL, "rowprep for expr ");
            SCIPprintConsExprExpr(scip, conshdlr, expr, NULL);
            SCIPinfoMessage(scip, NULL, "rowprep before perspectivy is: \n");
            SCIPprintRowprep(scip, rowprep, NULL);
#endif

            /* given a rowprep: sum aixi + sum biyi + c, where xi are semicontinuous variables and yi are
             * non-semicontinuous variables (which appear in expr linearly, which detect must have ensured),
             * perspectivy the semicontinuous part by adding (1-z)(g0 - c - sum aix0i) (the constant is
             * treated as belonging to the semicontinuous part)
             */

            /* we want cst0 = g0 - c - sum aix0i; first add g0 - c */
            cst0 = nlhdlrexprdata->exprvals0[i] + rowprep->side;

            maxcoef = 0.0;

            for( v = 0; v < rowprep->nvars; ++v )
            {
               if( REALABS( rowprep->coefs[v]) > maxcoef )
               {
                  maxcoef = REALABS(rowprep->coefs[v]);
               }

               scvdata = getSCVarDataInd(nlhdlrdata->scvars, rowprep->vars[v], indicator, &pos);

               /* a non-semicontinuous variable must be linear in expr; skip it */
               if( scvdata == NULL )
                  continue;

               cst0 -= rowprep->coefs[v] * scvdata->vals0[pos];
            }

            /* only perspectivy when the absolute value of cst0 is not too small
             * TODO on ex1252a there was cst0=0 - ok to still use the cut?
            */
            if( cst0 == 0.0 || maxcoef / REALABS(cst0) <= SCIP_CONSEXPR_CUTMAXRANGE )
            {
               /* update the rowprep by adding cst0 - cst0*z */
               SCIPaddRowprepConstant(rowprep, cst0);
               SCIP_CALL(SCIPaddRowprepTerm(scip, rowprep, indicator, -cst0));
            }
            else
            {
               SCIPfreeRowprep(scip, &rowprep);
               continue;
            }

            SCIP_CALL(SCIPaddRowprepTerm(scip, rowprep, auxvar, -1.0));

            SCIPdebugMsg(scip, "rowprep after perspectivy is: \n");
#ifdef SCIP_DEBUG
            SCIPprintRowprep(scip, rowprep, NULL);
#endif

            SCIP_CALL( SCIPsetPtrarrayVal(scip, rowpreps, nrowpreps, rowprep) );
            SCIP_CALL( SCIPsetBoolarrayVal(scip, addedbranchscores2, nrowpreps, addedbranchscores2j) );
            ++nrowpreps;
         }

         SCIP_CALL( SCIPclearPtrarray(scip, rowpreps2) );
      }

      if( nlhdlrdata->adjrefpoint )
         SCIP_CALL( SCIPfreeSol(scip, &soladj) );

      if( doprobingind )
      {
         SCIP_CALL( SCIPendProbing(scip) );
      }

      /* add all cuts found for indicator i */
      for( r = SCIPgetPtrarrayMinIdx(scip, rowpreps); r <= SCIPgetPtrarrayMaxIdx(scip, rowpreps) && !stop; ++r )
      {
         SCIP_RESULT resultr;

#ifdef SCIP_DEBUG
         SCIPprintRowprep(scip, rowprep, NULL);
#endif
         rowprep = (SCIP_ROWPREP*) SCIPgetPtrarrayVal(scip, rowpreps, r);
         resultr = SCIP_DIDNOTFIND;

         (void) strcat(rowprep->name, "_persp_indicator_");
         (void) strcat(rowprep->name, SCIPvarGetName(indicator));

         SCIP_CALL( SCIPprocessConsExprRowprep(scip, conshdlr, nlhdlr, cons, expr, rowprep, overestimate, auxvar,
               auxvalue, allowweakcuts, SCIPgetBoolarrayVal(scip, addedbranchscores2, r), addbranchscores, solcopy,
               &resultr) );

         if( resultr == SCIP_SEPARATED )
            *result = SCIP_SEPARATED;
         else if( resultr == SCIP_CUTOFF )
         {
            *result = SCIP_CUTOFF;
            stop = TRUE;
         }
         else if( resultr == SCIP_BRANCHED )
         {
            if( *result != SCIP_SEPARATED && *result != SCIP_REDUCEDDOM )
               *result = SCIP_BRANCHED;
         }
         else if( resultr != SCIP_DIDNOTFIND )
         {
            SCIPerrorMessage("estimate called by perspective nonlinear handler returned invalid result <%d>\n", resultr);
            return SCIP_INVALIDRESULT;
         }
      }

      /* free all rowpreps for indicator i */
      for( r = SCIPgetPtrarrayMinIdx(scip, rowpreps); r <= SCIPgetPtrarrayMaxIdx(scip, rowpreps); ++r )
      {
         rowprep = (SCIP_ROWPREP*) SCIPgetPtrarrayVal(scip, rowpreps, r);
         SCIPfreeRowprep(scip, &rowprep);
      }

      SCIPfreeBufferArrayNull(scip, &probingvars);
      SCIPfreeBufferArrayNull(scip, &probingdoms);
      SCIP_CALL( SCIPclearPtrarray(scip, rowpreps) );
   }

   /* generate cuts for the big-M indicators */
   for( i = 0; i < SCIPgetConsExprExprNBigMIndicators(expr); ++i )
   {
      int v;
      int minidx;
      int maxidx;
      int r;
      SCIP_VAR** probingvars;
      SCIP_INTERVAL* probingdoms;
      int nprobingvars;
      SCIP_Bool doprobingind;
      SCIP_Real indval;

      indicator = SCIPgetConsExprExprBigMIndicators(expr)[i];
      probingvars = NULL;
      probingdoms = NULL;
      nprobingvars = 0;
      doprobingind = doprobing;

      SCIP_CALL( analyseOnoffBounds(scip, nlhdlrdata, nlhdlrexprdata, indicator, &probingvars, &probingdoms,
                                    &nprobingvars, &doprobingind, result) );

      /* don't add perspective cuts for fixed indicators since there is no use for perspectivy */
      if( SCIPvarGetLbLocal(indicator) >= 0.5 )
      {
         assert(!doprobingind);
         continue;
      }
      if( SCIPvarGetUbLocal(indicator) <= 0.5 )
      { /* this case is stronger as it implies that everything is fixed;
         * therefore we are now happy
         */
         assert(!doprobingind);
         goto TERMINATE;
      }

      if( doprobingind )
      {
         SCIP_Bool propagate;
         SCIP_Bool cutoff_probing;
         SCIP_Bool cutoff;
         SCIP_Bool fixed;

#ifndef NDEBUG
         SCIP_Real* solvals;
         SCIP_CALL( SCIPallocBufferArray(scip, &solvals, nlhdlrexprdata->nvars) );
         for( v = 0; v < nlhdlrexprdata->nvars; ++v )
         {
            solvals[v] = SCIPgetSolVal(scip, sol, nlhdlrexprdata->vars[v]);
         }
#endif

         propagate = SCIPgetDepth(scip) == 0;

         SCIP_CALL( startProbing(scip, nlhdlrdata, nlhdlrexprdata, indicator, probingvars, probingdoms, nprobingvars,
                                 sol, &solcopy, &cutoff_probing) );

#ifndef NDEBUG
         for( v = 0; v < nlhdlrexprdata->nvars; ++v )
         {
            assert(solvals[v] == SCIPgetSolVal(scip, solcopy, nlhdlrexprdata->vars[v])); /*lint !e777*/
         }
         SCIPfreeBufferArray(scip, &solvals);
#endif

         if( propagate )
         { /* we are in the root node and startProbing did propagation */
            /* probing propagation might have detected infeasibility */
            if( cutoff_probing )
            {
               /* indicator == 1 is infeasible -> set indicator to 0 */
               SCIPfreeBufferArrayNull(scip, &probingvars);
               SCIPfreeBufferArrayNull(scip, &probingdoms);

               SCIP_CALL( SCIPendProbing(scip) );

               SCIP_CALL( SCIPfixVar(scip, indicator, 0.0, &cutoff, &fixed) );

               if( cutoff )
               {
                  *result = SCIP_CUTOFF;
                  goto TERMINATE;
               }

               continue;
            }

            /* probing propagation in the root node can provide better on/off bounds */
            SCIP_CALL( tightenOnBounds(nlhdlrexprdata, nlhdlrdata->scvars, indicator) );
         }
      }

      if( nlhdlrdata->adjrefpoint )
      {
         /* make sure that when we adjust the point, we don't divide by something too close to 0.0 */
         indval = MAX(SCIPgetSolVal(scip, solcopy, indicator), 0.1);

         /* create an adjusted point x^adj = (x* - x0) / z* + x0 */
         SCIP_CALL( SCIPcreateSol(scip, &soladj, NULL) );
         for( v = 0; v < nlhdlrexprdata->nvars; ++v )
         {
            if( SCIPvarGetStatus(nlhdlrexprdata->vars[v]) == SCIP_VARSTATUS_FIXED )
               continue;

            scvdata = getSCVarDataInd(nlhdlrdata->scvars, nlhdlrexprdata->vars[v], indicator, &pos);

            /* a non-semicontinuous variable will keep its value in soladj */
            if( scvdata == NULL )
            {
               SCIP_CALL( SCIPsetSolVal(scip, soladj, nlhdlrexprdata->vars[v],
                                        (SCIPgetSolVal(scip, solcopy, nlhdlrexprdata->vars[v]))) );
            }
            else
            {
               SCIP_CALL( SCIPsetSolVal(scip, soladj, nlhdlrexprdata->vars[v],
                                        (SCIPgetSolVal(scip, solcopy, nlhdlrexprdata->vars[v]) - scvdata->vals0[pos]) / indval
                                        + scvdata->vals0[pos]) );
            }

         }
         for( v = 0; v < SCIPgetConsExprExprNBigMIndicators(expr); ++v )
         {
            if( SCIPvarGetStatus(SCIPgetConsExprExprBigMIndicators(expr)[v]) == SCIP_VARSTATUS_FIXED )
               continue;

            SCIP_CALL( SCIPsetSolVal(scip, soladj, SCIPgetConsExprExprBigMIndicators(expr)[v],
                                     SCIPgetSolVal(scip, solcopy, SCIPgetConsExprExprBigMIndicators(expr)[v])) );
         }
         if( SCIPvarGetStatus(auxvar) != SCIP_VARSTATUS_FIXED )
            SCIP_CALL( SCIPsetSolVal(scip, soladj, auxvar, SCIPgetSolVal(scip, solcopy, auxvar)) );
      }

      /* use cuts from every suitable nlhdlr */
      for( j = 0; j < nenfos; ++j )
      {
         SCIP_Bool addedbranchscores2j;
         SCIP_CONSEXPR_NLHDLR* nlhdlr2;
         SCIP_CONSEXPR_NLHDLREXPRDATA* nlhdlr2exprdata;
         SCIP_Real nlhdlr2auxvalue;
         SCIP_Bool success2;
         SCIP_Real violation;
         SCIP_Real violationabs;
         SCIP_Real violationrel;

         SCIPgetConsExprExprEnfoData(expr, enfoposs[j], &nlhdlr2, &nlhdlr2exprdata, NULL, NULL, NULL, &nlhdlr2auxvalue);
         assert(SCIPhasConsExprNlhdlrEstimate(nlhdlr2) && nlhdlr2 != nlhdlr);

         if( nlhdlrdata->adjrefpoint )
         {
            SCIP_CALL( SCIPevalauxConsExprNlhdlr(scip, nlhdlr2, expr, nlhdlr2exprdata, &nlhdlr2auxvalue, soladj) );
            SCIPsetConsExprExprEnfoAuxValue(expr, j, nlhdlr2auxvalue);
         }

         SCIP_CALL( SCIPgetConsExprExprAbsAuxViolation(scip, conshdlr, expr, nlhdlr2auxvalue, nlhdlrdata->adjrefpoint ? soladj : solcopy, &violationabs, NULL, NULL) );
         SCIP_CALL( SCIPgetConsExprExprRelAuxViolation(scip, conshdlr, expr, nlhdlr2auxvalue, nlhdlrdata->adjrefpoint ? soladj : solcopy, &violationrel, NULL, NULL) );
         violation = MIN(violationabs, violationrel);

         SCIPdebugMsg(scip, "asking nonlinear handler %s to %sestimate\n", SCIPgetConsExprNlhdlrName(nlhdlr2), overestimate ? "over" : "under");

         /* ask the nonlinear handler for an estimator */
         if( nlhdlrdata->adjrefpoint )
         {
            SCIP_CALL( SCIPestimateConsExprNlhdlr(scip, conshdlr, nlhdlr2, expr,
                                                  nlhdlr2exprdata, soladj,
                                                  nlhdlr2auxvalue, overestimate, SCIPgetSolVal(scip, solcopy, auxvar),
                                                  rowpreps2, &success2, violation > 0 ? addbranchscores : FALSE, &addedbranchscores2j) );
         }
         else
         {
            SCIP_CALL( SCIPestimateConsExprNlhdlr(scip, conshdlr, nlhdlr2, expr,
                                                  nlhdlr2exprdata, solcopy,
                                                  nlhdlr2auxvalue, overestimate, SCIPgetSolVal(scip, solcopy, auxvar),
                                                  rowpreps2, &success2, violation > 0 ? addbranchscores : FALSE, &addedbranchscores2j) );
         }

         minidx = SCIPgetPtrarrayMinIdx(scip, rowpreps2);
         maxidx = SCIPgetPtrarrayMaxIdx(scip, rowpreps2);

         assert((success2 && minidx <= maxidx) || (!success2 && minidx > maxidx));

         /* perspectivy all cuts from nlhdlr2 and add them to rowpreps */
         for( r = minidx; r <= maxidx; ++r )
         {
            rowprep = (SCIP_ROWPREP*) SCIPgetPtrarrayVal(scip, rowpreps2, r);
            assert(rowprep != NULL);
            SCIP_CALL(SCIPaddRowprepTerm(scip, rowprep, auxvar, -1.0));

            /* find c to adjust the cut so that it touches at least one of the off vertices */
//            SCIPinfoMessage(scip, NULL, "\ncomputing cst0 for expr ");
//            SCIPprintConsExprExpr(scip, conshdlr, expr, NULL);
//            SCIPinfoMessage(scip, NULL, "\nfinding cst0 for cut ");
//            SCIPprintRowprep(scip, rowprep, NULL);
            SCIP_CALL( computeIndicatorCoef(scip, nlhdlrdata, rowprep, auxvar, indicator, SCIPgetConsExprExprBigMMax(expr), &cst0,
                                            SCIPgetConsExprExprBigMActivity(expr, i)) );

            /* adjust and add the cut */

            SCIP_Real maxcoef;

#ifdef SCIP_DEBUG
            SCIPinfoMessage(scip, NULL, "rowprep for expr ");
            SCIPprintConsExprExpr(scip, conshdlr, expr, NULL);
            SCIPinfoMessage(scip, NULL, "\nwith bounds: ");
            for( int bndi = 0; bndi < nlhdlrexprdata->nvars; ++bndi )
            {
               SCIPinfoMessage(scip, NULL, " %s in [%g,%g]", SCIPvarGetName(nlhdlrexprdata->vars[bndi]),
                               SCIPvarGetLbLocal(nlhdlrexprdata->vars[bndi]), SCIPvarGetUbLocal(nlhdlrexprdata->vars[bndi]));
            }
            SCIPinfoMessage(scip, NULL, " %s in [%g,%g] (max = %d)", SCIPvarGetName(auxvar),
                         SCIPvarGetLbLocal(auxvar), SCIPvarGetUbLocal(auxvar));
            SCIPinfoMessage(scip, NULL, "rowprep before perspectivy is: \n", SCIPgetConsExprExprBigMMax(expr));
            SCIPprintRowprep(scip, rowprep, NULL);
#endif
            /* add cst0(1-z) to the rowprep */

            maxcoef = 0.0;
            for( v = 0; v < rowprep->nvars; ++v )
            {
               if( REALABS( rowprep->coefs[v]) > maxcoef )
               {
                  maxcoef = REALABS(rowprep->coefs[v]);
               }
            }

            /* only perspectify when it strengthens the cut, and when the absolute value of cst0 is not too small */
            if( ((SCIPisPositive(scip, cst0) && rowprep->sidetype == SCIP_SIDETYPE_RIGHT) ||
                 (SCIPisNegative(scip, cst0) && rowprep->sidetype == SCIP_SIDETYPE_LEFT)) &&
                  maxcoef / REALABS(cst0) <= SCIP_CONSEXPR_CUTMAXRANGE &&
                  cst0 != SCIP_INVALID )
            {
               /* update the rowprep by adding cst0 - cst0*z */
               SCIPaddRowprepConstant(rowprep, cst0);
               SCIP_CALL( SCIPaddRowprepTerm(scip, rowprep, indicator, -cst0) );
            }
            else
            {
               SCIPfreeRowprep(scip, &rowprep);
               continue;
            }

#ifdef SCIP_DEBUG
            SCIPinfoMessage(scip, NULL, "rowprep after perspectify is: \n");
            SCIPprintRowprep(scip, rowprep, NULL);
#endif

            SCIP_CALL( SCIPsetPtrarrayVal(scip, rowpreps, nrowpreps, rowprep) );
            SCIP_CALL( SCIPsetBoolarrayVal(scip, addedbranchscores2, nrowpreps, addedbranchscores2j) );
            ++nrowpreps;
         }

         SCIP_CALL( SCIPclearPtrarray(scip, rowpreps2) );
      }

      if( nlhdlrdata->adjrefpoint )
         SCIP_CALL( SCIPfreeSol(scip, &soladj) );

      if( doprobingind )
      {
         SCIP_CALL( SCIPendProbing(scip) );
      }

      /* add all cuts found for indicator i */
      for( r = SCIPgetPtrarrayMinIdx(scip, rowpreps); r <= SCIPgetPtrarrayMaxIdx(scip, rowpreps) && !stop; ++r )
      {
         SCIP_RESULT resultr;

#ifdef SCIP_DEBUG
         SCIPprintRowprep(scip, rowprep, NULL);
#endif
         rowprep = (SCIP_ROWPREP*) SCIPgetPtrarrayVal(scip, rowpreps, r);
         resultr = SCIP_DIDNOTFIND;

         (void) strcat(rowprep->name, "_persp_indicator_");
         (void) strcat(rowprep->name, SCIPvarGetName(indicator));

         SCIP_CALL( SCIPprocessConsExprRowprep(scip, conshdlr, nlhdlr, cons, expr, rowprep, overestimate, auxvar,
                                               auxvalue, allowweakcuts, SCIPgetBoolarrayVal(scip, addedbranchscores2, r), addbranchscores, solcopy,
                                               &resultr) );

         if( resultr == SCIP_SEPARATED )
         {
            *result = SCIP_SEPARATED;
            ++nlhdlrdata->nbigmenfos;
         }
         else if( resultr == SCIP_CUTOFF )
         {
            *result = SCIP_CUTOFF;
            stop = TRUE;
            ++nlhdlrdata->nbigmenfos;
         }
         else if( resultr == SCIP_BRANCHED )
         {
            if( *result != SCIP_SEPARATED && *result != SCIP_REDUCEDDOM )
               *result = SCIP_BRANCHED;
         }
         else if( resultr != SCIP_DIDNOTFIND )
         {
            SCIPerrorMessage("estimate called by perspective nonlinear handler returned invalid result <%d>\n", resultr);
            return SCIP_INVALIDRESULT;
         }
      }

      /* free all rowpreps for indicator i */
      for( r = SCIPgetPtrarrayMinIdx(scip, rowpreps); r <= SCIPgetPtrarrayMaxIdx(scip, rowpreps); ++r )
      {
         rowprep = (SCIP_ROWPREP*) SCIPgetPtrarrayVal(scip, rowpreps, r);
         SCIPfreeRowprep(scip, &rowprep);
      }

      SCIPfreeBufferArrayNull(scip, &probingvars);
      SCIPfreeBufferArrayNull(scip, &probingdoms);
      SCIP_CALL( SCIPclearPtrarray(scip, rowpreps) );
   }

TERMINATE:
   SCIP_CALL( SCIPfreeBoolarray(scip, &addedbranchscores2) );
   SCIP_CALL( SCIPfreePtrarray(scip, &rowpreps) );
   SCIP_CALL( SCIPfreePtrarray(scip, &rowpreps2) );
   if( solcopy != sol )
   {
      SCIP_CALL( SCIPfreeSol(scip, &solcopy) );
   }
   SCIPfreeBufferArray(scip, &enfoposs);

   return SCIP_OKAY;
}

/** nonlinear handler callback for reformulation */
#if 0
static
SCIP_DECL_CONSEXPR_NLHDLRREFORMULATE(nlhdlrReformulatePerspective)
{ /*lint --e{715}*/

   /* set refexpr to expr and capture it if no reformulation is possible */
   *refexpr = expr;
   SCIPcaptureConsExprExpr(*refexpr);

   return SCIP_OKAY;
}
#endif

/*
 * nonlinear handler specific interface methods
 */

/** includes Perspective nonlinear handler to consexpr */
SCIP_RETCODE SCIPincludeConsExprNlhdlrPerspective(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr        /**< expression constraint handler */
   )
{
   SCIP_CONSEXPR_NLHDLRDATA* nlhdlrdata;
   SCIP_CONSEXPR_NLHDLR* nlhdlr;

   assert(scip != NULL);
   assert(consexprhdlr != NULL);

   /* create nonlinear handler data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &nlhdlrdata) );
   BMSclearMemory(nlhdlrdata);

   SCIP_CALL( SCIPincludeConsExprNlhdlrBasic(scip, consexprhdlr, &nlhdlr, NLHDLR_NAME, NLHDLR_DESC, NLHDLR_DETECTPRIORITY,
      NLHDLR_ENFOPRIORITY, nlhdlrDetectPerspective, nlhdlrEvalauxPerspective, nlhdlrdata) );
   assert(nlhdlr != NULL);

   SCIP_CALL( SCIPaddIntParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/maxproprounds",
           "maximal number of propagation rounds in probing",
           &nlhdlrdata->maxproprounds, FALSE, DEFAULT_MAXPROPROUNDS, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/mindomreduction",
           "minimal relative reduction in a variable's domain for applying probing",
           &nlhdlrdata->mindomreduction, FALSE, DEFAULT_MINDOMREDUCTION, 0.0, 1.0, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/minviolprobing",
           "minimal violation w.r.t. auxiliary variables for applying probing",
           &nlhdlrdata->minviolprobing, FALSE, DEFAULT_MINVIOLPROBING, 0.0, SCIP_REAL_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/probingonlyinsepa",
           "whether to do probing only in separation",
           &nlhdlrdata->probingonlyinsepa, FALSE, DEFAULT_PROBINGONLYINSEPA, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/probingfreq",
           "probing frequency (-1 - no probing, 0 - root node only)",
           &nlhdlrdata->probingfreq, FALSE, DEFAULT_PROBINGFREQ, -1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/convexonly",
           "whether perspective cuts are added only for convex expressions",
           &nlhdlrdata->convexonly, FALSE, DEFAULT_CONVEXONLY, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/tightenbounds",
           "whether variable semicontinuity is used to tighten variable bounds",
           &nlhdlrdata->tightenbounds, FALSE, DEFAULT_TIGHTENBOUNDS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/adjrefpoint",
           "whether to adjust the reference point",
           &nlhdlrdata->adjrefpoint, FALSE, DEFAULT_ADJREFPOINT, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/expr/nlhdlr/" NLHDLR_NAME "/bigmcuts",
         "whether to strengthen cuts for constraints with big-M structure",
         &nlhdlrdata->bigmcuts, FALSE, DEFAULT_BIGMCUTS, NULL, NULL) );


   SCIPsetConsExprNlhdlrCopyHdlr(scip, nlhdlr, nlhdlrCopyhdlrPerspective);
   SCIPsetConsExprNlhdlrFreeHdlrData(scip, nlhdlr, nlhdlrFreehdlrdataPerspective);
   SCIPsetConsExprNlhdlrFreeExprData(scip, nlhdlr, nlhdlrFreeExprDataPerspective);
   SCIPsetConsExprNlhdlrInitExit(scip, nlhdlr, nlhdlrInitPerspective, nlhdlrExitPerspective);
   SCIPsetConsExprNlhdlrSepa(scip, nlhdlr, nlhdlrInitSepaPerspective, nlhdlrEnfoPerspective, NULL, NULL);

   return SCIP_OKAY;
}
