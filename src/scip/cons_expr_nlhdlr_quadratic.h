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

/**@file   cons_expr_nlhdlr_quadratic.h
 * @brief  nonlinear handler to handle quadratic expressions
 * @author Felipe Serrano
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_CONS_EXPR_NLHDLR_QUADRATIC_H__
#define __SCIP_CONS_EXPR_NLHDLR_QUADRATIC_H__


#include "scip/scip.h"
#include "scip/cons_expr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** includes quadratic nonlinear handler to consexpr */
SCIP_EXPORT
SCIP_RETCODE SCIPincludeConsExprNlhdlrQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr        /**< expression constraint handler */
   );

#ifdef __cplusplus
}
#endif

#endif /* __SCIP_CONS_EXPR_NLHDLR_QUADRATIC_H__ */
