/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2022 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/*    Authors: Julian Hall, Ivet Galabova, Leona Gottwald and Michael    */
/*    Feldmeier                                                          */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**@file simplex/HighsLpSolverObject.h
 * @brief Collection of class instances required to solve an LP
 */
#ifndef LP_DATA_HIGHS_LP_SOLVER_OBJECT_H_
#define LP_DATA_HIGHS_LP_SOLVER_OBJECT_H_

#include "HighsClbk.h"
#include "lp_data/HighsInfo.h"
#include "lp_data/HighsOptions.h"
#include "simplex/HEkk.h"

class HighsLpSolverObject {
 public:
  HighsLpSolverObject(HighsLp& lp, HighsBasis& basis, HighsSolution& solution,
                      HighsInfo& highs_info, HEkk& ekk_instance,
                      HighsOptions& options, HighsTimer& timer,
                      HighsClbk clbk_fun)
      : lp_(lp),
        basis_(basis),
        solution_(solution),
        highs_info_(highs_info),
        ekk_instance_(ekk_instance),
        options_(options),
        timer_(timer),
        clbk_fun_(clbk_fun) {}

  HighsLp& lp_;
  HighsBasis& basis_;
  HighsSolution& solution_;
  HighsInfo& highs_info_;
  HEkk& ekk_instance_;
  HighsOptions& options_;
  HighsTimer& timer_;
  HighsClbk clbk_fun_;

  HighsModelStatus model_status_ = HighsModelStatus::kNotset;
};

#endif  // LP_DATA_HIGHS_LP_SOLVER_OBJECT_H_
