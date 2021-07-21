/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2021 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/*    Authors: Julian Hall, Ivet Galabova, Qi Huangfu, Leona Gottwald    */
/*    and Michael Feldmeier                                              */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "mip/HighsSearch.h"

#include <numeric>

#include "lp_data/HConst.h"
#include "mip/HighsCutGeneration.h"
#include "mip/HighsDomainChange.h"
#include "mip/HighsMipSolverData.h"

HighsSearch::HighsSearch(HighsMipSolver& mipsolver,
                         const HighsPseudocost& pseudocost)
    : mipsolver(mipsolver),
      lp(nullptr),
      localdom(mipsolver.mipdata_->domain),
      pseudocost(pseudocost) {
  nnodes = 0;
  treeweight = 0.0;
  depthoffset = 0;
  lpiterations = 0;
  heurlpiterations = 0;
  sblpiterations = 0;
  upper_limit = kHighsInf;
  inheuristic = false;
  inbranching = false;
  childselrule = mipsolver.submip ? ChildSelectionRule::kHybridInferenceCost
                                  : ChildSelectionRule::kRootSol;
  this->localdom.setDomainChangeStack(std::vector<HighsDomainChange>());
}

double HighsSearch::checkSol(const std::vector<double>& sol,
                             bool& integerfeasible) const {
  HighsCDouble objval = 0.0;
  integerfeasible = true;
  for (HighsInt i = 0; i != mipsolver.numCol(); ++i) {
    objval += sol[i] * mipsolver.colCost(i);
    assert(std::isfinite(sol[i]));

    if (!integerfeasible || mipsolver.variableType(i) != HighsVarType::kInteger)
      continue;

    double intval = std::floor(sol[i] + 0.5);
    if (std::abs(sol[i] - intval) > mipsolver.mipdata_->feastol) {
      integerfeasible = false;
    }
  }

  return double(objval);
}

bool HighsSearch::orbitsValidInChildNode(
    const HighsDomainChange& branchChg) const {
  HighsInt branchCol = branchChg.column;
  // if the variable is integral or we are in an up branch the stabilizer only
  // stays valid if the column has been stabilized
  const NodeData& currNode = nodestack.back();
  if (!currNode.stabilizerOrbits ||
      currNode.stabilizerOrbits->orbitCols.empty() ||
      currNode.stabilizerOrbits->isStabilized(branchCol))
    return true;

  // a down branch stays valid if the variable is binary
  if (branchChg.boundtype == HighsBoundType::kUpper &&
      localdom.isGlobalBinary(branchChg.column))
    return true;

  return false;
}

double HighsSearch::getCutoffBound() const {
  return std::min(mipsolver.mipdata_->upper_limit, upper_limit);
}

void HighsSearch::setRINSNeighbourhood(const std::vector<double>& basesol,
                                       const std::vector<double>& relaxsol) {
  for (HighsInt i = 0; i != mipsolver.numCol(); ++i) {
    if (mipsolver.variableType(i) != HighsVarType::kInteger) continue;
    if (localdom.colLower_[i] == localdom.colUpper_[i]) continue;

    double intval = std::floor(basesol[i] + 0.5);
    if (std::abs(relaxsol[i] - intval) < mipsolver.mipdata_->feastol) {
      if (localdom.colLower_[i] < intval)
        localdom.changeBound(HighsBoundType::kLower, i,
                             std::min(intval, localdom.colUpper_[i]),
                             HighsDomain::Reason::unspecified());
      if (localdom.colUpper_[i] > intval)
        localdom.changeBound(HighsBoundType::kUpper, i,
                             std::max(intval, localdom.colLower_[i]),
                             HighsDomain::Reason::unspecified());
    }
  }
}

void HighsSearch::setRENSNeighbourhood(const std::vector<double>& lpsol) {
  for (HighsInt i = 0; i != mipsolver.numCol(); ++i) {
    if (mipsolver.variableType(i) != HighsVarType::kInteger) continue;
    if (localdom.colLower_[i] == localdom.colUpper_[i]) continue;

    double downval = std::floor(lpsol[i] + mipsolver.mipdata_->feastol);
    double upval = std::ceil(lpsol[i] - mipsolver.mipdata_->feastol);

    if (localdom.colLower_[i] < downval) {
      localdom.changeBound(HighsBoundType::kLower, i,
                           std::min(downval, localdom.colUpper_[i]),
                           HighsDomain::Reason::unspecified());
      if (localdom.infeasible()) return;
    }
    if (localdom.colUpper_[i] > upval) {
      localdom.changeBound(HighsBoundType::kUpper, i,
                           std::max(upval, localdom.colLower_[i]),
                           HighsDomain::Reason::unspecified());
      if (localdom.infeasible()) return;
    }
  }
}

void HighsSearch::createNewNode() {
  nodestack.emplace_back();
  nodestack.back().domgchgStackPos = localdom.getDomainChangeStack().size();
}

void HighsSearch::cutoffNode() { nodestack.back().opensubtrees = 0; }

void HighsSearch::setMinReliable(HighsInt minreliable) {
  pseudocost.setMinReliable(minreliable);
}

void HighsSearch::branchDownwards(HighsInt col, double newub,
                                  double branchpoint) {
  NodeData& currnode = nodestack.back();

  assert(currnode.opensubtrees == 2);
  assert(mipsolver.variableType(col) != HighsVarType::kContinuous);

  currnode.opensubtrees = 1;
  currnode.branching_point = branchpoint;
  currnode.branchingdecision.column = col;
  currnode.branchingdecision.boundval = newub;
  currnode.branchingdecision.boundtype = HighsBoundType::kUpper;

  HighsInt domchgPos = localdom.getDomainChangeStack().size();
  bool passStabilizerToChildNode =
      orbitsValidInChildNode(currnode.branchingdecision);
  localdom.changeBound(currnode.branchingdecision);
  nodestack.emplace_back(
      currnode.lower_bound, currnode.estimate, currnode.nodeBasis,
      passStabilizerToChildNode ? currnode.stabilizerOrbits : nullptr);
  nodestack.back().domgchgStackPos = domchgPos;
}

void HighsSearch::branchUpwards(HighsInt col, double newlb,
                                double branchpoint) {
  NodeData& currnode = nodestack.back();

  assert(currnode.opensubtrees == 2);
  assert(mipsolver.variableType(col) != HighsVarType::kContinuous);

  currnode.opensubtrees = 1;
  currnode.branching_point = branchpoint;
  currnode.branchingdecision.column = col;
  currnode.branchingdecision.boundval = newlb;
  currnode.branchingdecision.boundtype = HighsBoundType::kLower;

  HighsInt domchgPos = localdom.getDomainChangeStack().size();
  bool passStabilizerToChildNode =
      orbitsValidInChildNode(currnode.branchingdecision);
  localdom.changeBound(currnode.branchingdecision);
  nodestack.emplace_back(
      currnode.lower_bound, currnode.estimate, currnode.nodeBasis,
      passStabilizerToChildNode ? currnode.stabilizerOrbits : nullptr);
  nodestack.back().domgchgStackPos = domchgPos;
}

void HighsSearch::addBoundExceedingConflict() {
  if (mipsolver.mipdata_->upper_limit != kHighsInf) {
    double rhs;
    if (lp->computeDualProof(mipsolver.mipdata_->domain,
                             mipsolver.mipdata_->upper_limit, inds, vals,
                             rhs)) {
      if (mipsolver.mipdata_->domain.infeasible()) return;
      localdom.conflictAnalysis(inds.data(), vals.data(), inds.size(), rhs,
                                mipsolver.mipdata_->conflictPool);

      HighsCutGeneration cutGen(*lp, mipsolver.mipdata_->cutpool);
      mipsolver.mipdata_->debugSolution.checkCut(inds.data(), vals.data(),
                                                 inds.size(), rhs);
      cutGen.generateConflict(localdom, inds, vals, rhs);
    }
  }
}

void HighsSearch::addInfeasibleConflict() {
  double rhs;
  if (lp->computeDualInfProof(mipsolver.mipdata_->domain, inds, vals, rhs)) {
    if (mipsolver.mipdata_->domain.infeasible()) return;
    // double minactlocal = 0.0;
    // double minactglobal = 0.0;
    // for (HighsInt i = 0; i < int(inds.size()); ++i) {
    //  if (vals[i] > 0.0) {
    //    minactlocal += localdom.colLower_[inds[i]] * vals[i];
    //    minactglobal += globaldom.colLower_[inds[i]] * vals[i];
    //  } else {
    //    minactlocal += localdom.colUpper_[inds[i]] * vals[i];
    //    minactglobal += globaldom.colUpper_[inds[i]] * vals[i];
    //  }
    //}
    // HighsInt oldnumcuts = cutpool.getNumCuts();
    localdom.conflictAnalysis(inds.data(), vals.data(), inds.size(), rhs,
                              mipsolver.mipdata_->conflictPool);

    HighsCutGeneration cutGen(*lp, mipsolver.mipdata_->cutpool);
    mipsolver.mipdata_->debugSolution.checkCut(inds.data(), vals.data(),
                                               inds.size(), rhs);
    cutGen.generateConflict(localdom, inds, vals, rhs);

    // if (cutpool.getNumCuts() > oldnumcuts) {
    //  printf(
    //      "added cut from infeasibility proof with local min activity %g, "
    //      "global min activity %g, and rhs %g\n",
    //      minactlocal, minactglobal, rhs);
    //} else {
    //  printf(
    //      "no cut found for infeasibility proof with local min activity %g, "
    //      "global min "
    //      " activity %g, and rhs % g\n ",
    //      minactlocal, minactglobal, rhs);
    //}
    // HighsInt cutind = cutpool.addCut(inds.data(), vals.data(), inds.size(),
    // rhs); localdom.cutAdded(cutind);
  }
}

HighsInt HighsSearch::selectBranchingCandidate(int64_t maxSbIters) {
  assert(!lp->getFractionalIntegers().empty());

  static constexpr HighsInt basisstart_threshold = 20;
  std::vector<double> upscore;
  std::vector<double> downscore;
  std::vector<uint8_t> upscorereliable;
  std::vector<uint8_t> downscorereliable;

  HighsInt numfrac = lp->getFractionalIntegers().size();
  const auto& fracints = lp->getFractionalIntegers();

  upscore.resize(numfrac, kHighsInf);
  downscore.resize(numfrac, kHighsInf);

  upscorereliable.resize(numfrac, 0);
  downscorereliable.resize(numfrac, 0);

  // initialize up and down scores of variables that have a
  // reliable pseudocost so that they do not get evaluated
  for (HighsInt k = 0; k != numfrac; ++k) {
    HighsInt col = fracints[k].first;
    double fracval = fracints[k].second;

    assert(fracval > localdom.colLower_[col] + mipsolver.mipdata_->feastol);
    assert(fracval < localdom.colUpper_[col] - mipsolver.mipdata_->feastol);

    if (pseudocost.isReliable(col) || branchingVarReliableAtNode(col)) {
      upscore[k] = pseudocost.getPseudocostUp(col, fracval);
      downscore[k] = pseudocost.getPseudocostDown(col, fracval);
      upscorereliable[k] = true;
      downscorereliable[k] = true;
    }
  }

  std::vector<HighsInt> evalqueue;
  evalqueue.resize(numfrac);
  std::iota(evalqueue.begin(), evalqueue.end(), 0);

  auto numNodesUp = [&](HighsInt k) {
    return mipsolver.mipdata_->nodequeue.numNodesUp(fracints[k].first);
  };

  auto numNodesDown = [&](HighsInt k) {
    return mipsolver.mipdata_->nodequeue.numNodesDown(fracints[k].first);
  };

  double minScore = mipsolver.mipdata_->feastol;

  auto selectBestScore = [&](bool finalSelection) {
    HighsInt best = -1;
    double bestscore = -1.0;
    double bestnodes = -1.0;
    int64_t bestnumnodes = 0;

    double oldminscore = minScore;
    for (HighsInt k : evalqueue) {
      double score;

      if (upscore[k] <= oldminscore) upscorereliable[k] = 1;
      if (downscore[k] <= oldminscore) downscorereliable[k] = 1;

      double s = 1e-3 * std::min(upscorereliable[k] ? upscore[k] : 0,
                                 downscorereliable[k] ? downscore[k] : 0);
      minScore = std::max(s, minScore);

      if (upscore[k] <= oldminscore || downscore[k] <= oldminscore)
        score = pseudocost.getScore(fracints[k].first,
                                    std::min(upscore[k], oldminscore),
                                    std::min(downscore[k], oldminscore));
      else {
        score = upscore[k] == kHighsInf || downscore[k] == kHighsInf
                    ? finalSelection ? pseudocost.getScore(fracints[k].first,
                                                           fracints[k].second)
                                     : kHighsInf
                    : pseudocost.getScore(fracints[k].first, upscore[k],
                                          downscore[k]);
      }

      assert(score >= 0.0);
      int64_t upnodes = numNodesUp(k);
      int64_t downnodes = numNodesDown(k);
      double nodes = 0;
      int64_t numnodes = upnodes + downnodes;
      if (upnodes != 0 || downnodes != 0)
        nodes =
            (downnodes / (double)(numnodes)) * (upnodes / (double)(numnodes));
      if (score > bestscore ||
          (score > bestscore - mipsolver.mipdata_->feastol &&
           std::make_pair(nodes, numnodes) >
               std::make_pair(bestnodes, bestnumnodes))) {
        bestscore = score;
        best = k;
        bestnodes = nodes;
        bestnumnodes = numnodes;
      }
    }

    return best;
  };

  bool resetBasis = false;

  while (true) {
    bool mustStop = getStrongBranchingLpIterations() >= maxSbIters ||
                    mipsolver.mipdata_->checkLimits();

    HighsInt candidate = selectBestScore(mustStop);

    if ((upscorereliable[candidate] && downscorereliable[candidate]) ||
        mustStop) {
      if (resetBasis) {
        lp->setStoredBasis(nodestack.back().nodeBasis);
        lp->recoverBasis();
        lp->run();
      }
      return candidate;
    }

    lp->setObjectiveLimit(mipsolver.mipdata_->upper_limit);

    HighsInt col = fracints[candidate].first;
    double fracval = fracints[candidate].second;
    double upval = std::ceil(fracval);
    double downval = std::floor(fracval);

    if (!downscorereliable[candidate]) {
      // evaluate down branch
      int64_t inferences = -(int64_t)localdom.getDomainChangeStack().size() - 1;

      HighsDomainChange domchg{downval, col, HighsBoundType::kUpper};
      bool orbitalFixing =
          nodestack.back().stabilizerOrbits && orbitsValidInChildNode(domchg);
      localdom.changeBound(domchg);
      localdom.propagate();

      if (localdom.infeasible()) orbitalFixing = false;

      if (orbitalFixing) {
        HighsInt numFix =
            nodestack.back().stabilizerOrbits->orbitalFixing(localdom);
        if (numFix == 0) orbitalFixing = false;
      }

      inferences += localdom.getDomainChangeStack().size();
      if (localdom.infeasible()) {
        localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
        pseudocost.addCutoffObservation(col, false);
        localdom.backtrack();
        localdom.clearChangedCols();

        branchUpwards(col, upval, fracval);
        nodestack[nodestack.size() - 2].opensubtrees = 0;
        nodestack[nodestack.size() - 2].skipDepthCount = 1;
        depthoffset -= 1;

        lp->setStoredBasis(nodestack.back().nodeBasis);
        return -1;
      }

      pseudocost.addInferenceObservation(col, inferences, false);

      lp->flushDomain(localdom);

      resetBasis = true;
      int64_t numiters = lp->getNumLpIterations();
      HighsLpRelaxation::Status status = lp->run(false);
      numiters = lp->getNumLpIterations() - numiters;
      lpiterations += numiters;
      sblpiterations += numiters;

      if (lp->scaledOptimal(status)) {
        lp->resetAges();

        double delta = downval - fracval;
        bool integerfeasible;
        const std::vector<double>& sol =
            lp->getLpSolver().getSolution().col_value;
        double solobj = checkSol(sol, integerfeasible);

        double objdelta = std::max(solobj - lp->getObjective(), 0.0);
        if (objdelta <= mipsolver.mipdata_->epsilon) objdelta = 0.0;

        downscore[candidate] = objdelta;
        downscorereliable[candidate] = 1;
        markBranchingVarDownReliableAtNode(col);
        pseudocost.addObservation(col, delta, objdelta);

        for (HighsInt k = 0; k != numfrac; ++k) {
          double otherfracval = fracints[k].second;
          double otherdownval = std::floor(fracints[k].second);
          double otherupval = std::ceil(fracints[k].second);
          if (sol[fracints[k].first] <=
              otherdownval + mipsolver.mipdata_->feastol) {
            if (objdelta <= minScore &&
                localdom.colUpper_[fracints[k].first] <=
                    otherdownval + mipsolver.mipdata_->feastol)
              pseudocost.addObservation(fracints[k].first,
                                        otherdownval - otherfracval, objdelta);
            downscore[k] = std::min(downscore[k], objdelta);
          } else if (sol[fracints[k].first] >=
                     otherupval - mipsolver.mipdata_->feastol) {
            if (objdelta <= minScore &&
                localdom.colLower_[fracints[k].first] >=
                    otherupval - mipsolver.mipdata_->feastol)
              pseudocost.addObservation(fracints[k].first,
                                        otherupval - otherfracval, objdelta);
            upscore[k] = std::min(upscore[k], objdelta);
          }
        }

        if (lp->unscaledPrimalFeasible(status) && integerfeasible) {
          double cutoffbnd = getCutoffBound();
          mipsolver.mipdata_->addIncumbent(
              lp->getLpSolver().getSolution().col_value, solobj,
              inheuristic ? 'H' : 'B');

          if (mipsolver.mipdata_->upper_limit < cutoffbnd)
            lp->setObjectiveLimit(mipsolver.mipdata_->upper_limit);
        }

        if (lp->unscaledDualFeasible(status)) {
          if (solobj > getCutoffBound()) {
            mipsolver.mipdata_->debugSolution.nodePruned(localdom);
            addBoundExceedingConflict();
            localdom.backtrack();
            lp->flushDomain(localdom);

            branchUpwards(col, upval, fracval);
            nodestack[nodestack.size() - 2].opensubtrees = 0;
            nodestack[nodestack.size() - 2].skipDepthCount = 1;
            depthoffset -= 1;

            lp->setStoredBasis(nodestack.back().nodeBasis);
            if (numiters > basisstart_threshold) lp->recoverBasis();
            return -1;
          }
        } else if (solobj > getCutoffBound()) {
          addBoundExceedingConflict();
          localdom.propagate();
          bool infeas = localdom.infeasible();
          if (infeas) {
            localdom.backtrack();
            lp->flushDomain(localdom);

            branchUpwards(col, upval, fracval);
            nodestack[nodestack.size() - 2].opensubtrees = 0;
            nodestack[nodestack.size() - 2].skipDepthCount = 1;
            depthoffset -= 1;

            lp->setStoredBasis(nodestack.back().nodeBasis);
            if (numiters > basisstart_threshold) lp->recoverBasis();
            return -1;
          }
        }
      } else if (status == HighsLpRelaxation::Status::kInfeasible) {
        mipsolver.mipdata_->debugSolution.nodePruned(localdom);
        addInfeasibleConflict();
        pseudocost.addCutoffObservation(col, false);
        localdom.backtrack();
        lp->flushDomain(localdom);

        branchUpwards(col, upval, fracval);
        nodestack[nodestack.size() - 2].opensubtrees = 0;
        nodestack[nodestack.size() - 2].skipDepthCount = 1;
        depthoffset -= 1;

        lp->setStoredBasis(nodestack.back().nodeBasis);
        if (numiters > basisstart_threshold) lp->recoverBasis();
        return -1;
      } else {
        // printf("todo2\n");
        // in case of an LP error we set the score of this variable to zero to
        // avoid choosing it as branching candidate if possible
        downscore[candidate] = 0.0;
        upscore[candidate] = 0.0;
        downscorereliable[candidate] = 1;
        upscorereliable[candidate] = 1;
        markBranchingVarUpReliableAtNode(col);
        markBranchingVarDownReliableAtNode(col);
      }

      localdom.backtrack();
      lp->flushDomain(localdom);
      if (numiters > basisstart_threshold) lp->recoverBasis();
    } else {
      // evaluate up branch
      int64_t inferences = -(int64_t)localdom.getDomainChangeStack().size() - 1;
      HighsDomainChange domchg{upval, col, HighsBoundType::kLower};
      bool orbitalFixing =
          nodestack.back().stabilizerOrbits && orbitsValidInChildNode(domchg);
      localdom.changeBound(domchg);
      localdom.propagate();

      if (localdom.infeasible()) orbitalFixing = false;

      if (orbitalFixing)
        nodestack.back().stabilizerOrbits->orbitalFixing(localdom);

      inferences += localdom.getDomainChangeStack().size();
      if (localdom.infeasible()) {
        localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
        pseudocost.addCutoffObservation(col, true);
        localdom.backtrack();
        localdom.clearChangedCols();

        branchDownwards(col, downval, fracval);
        nodestack[nodestack.size() - 2].opensubtrees = 0;
        nodestack[nodestack.size() - 2].skipDepthCount = 1;
        depthoffset -= 1;

        lp->setStoredBasis(nodestack.back().nodeBasis);
        return -1;
      }

      pseudocost.addInferenceObservation(col, inferences, true);
      lp->flushDomain(localdom);

      resetBasis = true;
      int64_t numiters = lp->getNumLpIterations();
      HighsLpRelaxation::Status status = lp->run(false);
      numiters = lp->getNumLpIterations() - numiters;
      lpiterations += numiters;
      sblpiterations += numiters;

      if (lp->scaledOptimal(status)) {
        lp->resetAges();
        double delta = upval - fracval;
        bool integerfeasible;

        const std::vector<double>& sol =
            lp->getLpSolver().getSolution().col_value;
        double solobj = checkSol(sol, integerfeasible);

        double objdelta = std::max(solobj - lp->getObjective(), 0.0);
        if (objdelta <= mipsolver.mipdata_->epsilon) objdelta = 0.0;

        upscore[candidate] = objdelta;
        upscorereliable[candidate] = 1;
        markBranchingVarUpReliableAtNode(col);
        pseudocost.addObservation(col, delta, objdelta);

        for (HighsInt k = 0; k != numfrac; ++k) {
          double otherfracval = fracints[k].second;
          double otherdownval = std::floor(fracints[k].second);
          double otherupval = std::ceil(fracints[k].second);
          if (sol[fracints[k].first] <=
              otherdownval + mipsolver.mipdata_->feastol) {
            if (objdelta <= minScore &&
                localdom.colUpper_[fracints[k].first] <=
                    otherdownval + mipsolver.mipdata_->feastol)
              pseudocost.addObservation(fracints[k].first,
                                        otherdownval - otherfracval, objdelta);
            downscore[k] = std::min(downscore[k], objdelta);

          } else if (sol[fracints[k].first] >=
                     otherupval - mipsolver.mipdata_->feastol) {
            if (objdelta <= minScore &&
                localdom.colLower_[fracints[k].first] >=
                    otherupval - mipsolver.mipdata_->feastol)
              pseudocost.addObservation(fracints[k].first,
                                        otherupval - otherfracval, objdelta);
            upscore[k] = std::min(upscore[k], objdelta);
          }
        }

        if (lp->unscaledPrimalFeasible(status) && integerfeasible) {
          double cutoffbnd = getCutoffBound();
          mipsolver.mipdata_->addIncumbent(
              lp->getLpSolver().getSolution().col_value, solobj,
              inheuristic ? 'H' : 'B');

          if (mipsolver.mipdata_->upper_limit < cutoffbnd)
            lp->setObjectiveLimit(mipsolver.mipdata_->upper_limit);
        }

        if (lp->unscaledDualFeasible(status)) {
          if (solobj > getCutoffBound()) {
            mipsolver.mipdata_->debugSolution.nodePruned(localdom);
            addBoundExceedingConflict();
            localdom.backtrack();
            lp->flushDomain(localdom);

            branchDownwards(col, downval, fracval);
            nodestack[nodestack.size() - 2].opensubtrees = 0;
            nodestack[nodestack.size() - 2].skipDepthCount = 1;
            depthoffset -= 1;

            lp->setStoredBasis(nodestack.back().nodeBasis);
            if (numiters > basisstart_threshold) lp->recoverBasis();
            return -1;
          }
        } else if (solobj > getCutoffBound()) {
          addBoundExceedingConflict();
          localdom.propagate();
          bool infeas = localdom.infeasible();
          if (infeas) {
            localdom.backtrack();
            lp->flushDomain(localdom);

            branchDownwards(col, downval, fracval);
            nodestack[nodestack.size() - 2].opensubtrees = 0;
            nodestack[nodestack.size() - 2].skipDepthCount = 1;
            depthoffset -= 1;

            lp->setStoredBasis(nodestack.back().nodeBasis);
            if (numiters > basisstart_threshold) lp->recoverBasis();
            return -1;
          }
        }
      } else if (status == HighsLpRelaxation::Status::kInfeasible) {
        mipsolver.mipdata_->debugSolution.nodePruned(localdom);
        addInfeasibleConflict();
        pseudocost.addCutoffObservation(col, true);
        localdom.backtrack();
        lp->flushDomain(localdom);

        branchDownwards(col, downval, fracval);
        nodestack[nodestack.size() - 2].opensubtrees = 0;
        nodestack[nodestack.size() - 2].skipDepthCount = 1;
        depthoffset -= 1;

        lp->setStoredBasis(nodestack.back().nodeBasis);
        if (numiters > basisstart_threshold) lp->recoverBasis();
        return -1;
      } else {
        // printf("todo2\n");
        // in case of an LP error we set the score of this variable to zero to
        // avoid choosing it as branching candidate if possible
        downscore[candidate] = 0.0;
        upscore[candidate] = 0.0;
        downscorereliable[candidate] = 1;
        upscorereliable[candidate] = 1;
        markBranchingVarUpReliableAtNode(col);
        markBranchingVarDownReliableAtNode(col);
      }

      localdom.backtrack();
      lp->flushDomain(localdom);
      if (numiters > basisstart_threshold) lp->recoverBasis();
    }
  }
}

const HighsSearch::NodeData* HighsSearch::getParentNodeData() const {
  if (nodestack.size() <= 1) return nullptr;

  return &nodestack[nodestack.size() - 2];
}

void HighsSearch::currentNodeToQueue(HighsNodeQueue& nodequeue) {
  auto oldchangedcols = localdom.getChangedCols().size();
  bool prune = nodestack.back().lower_bound > getCutoffBound();
  if (!prune) {
    localdom.propagate();
    localdom.clearChangedCols(oldchangedcols);
    prune = localdom.infeasible();
    if (prune) localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
  }
  if (!prune) {
    std::vector<HighsInt> branchPositions;
    auto domchgStack = localdom.getReducedDomainChangeStack(branchPositions);
    nodequeue.emplaceNode(std::move(domchgStack), std::move(branchPositions),
                          nodestack.back().lower_bound,
                          nodestack.back().estimate, getCurrentDepth());
  } else
    treeweight += std::pow(0.5, getCurrentDepth() - 1);
  nodestack.back().opensubtrees = 0;

  backtrack();
  lp->flushDomain(localdom);
  if (!nodestack.empty() && nodestack.back().nodeBasis) {
    lp->setStoredBasis(nodestack.back().nodeBasis);
    lp->recoverBasis();
  }
}

void HighsSearch::openNodesToQueue(HighsNodeQueue& nodequeue) {
  if (nodestack.empty()) return;

  std::shared_ptr<const HighsBasis> basis;
  if (nodestack.back().opensubtrees == 0) {
    if (nodestack.back().nodeBasis)
      basis = std::move(nodestack.back().nodeBasis);
    backtrack(false);
  }

  while (!nodestack.empty()) {
    auto oldchangedcols = localdom.getChangedCols().size();
    bool prune = nodestack.back().lower_bound > getCutoffBound();
    if (!prune) {
      localdom.propagate();
      localdom.clearChangedCols(oldchangedcols);
      prune = localdom.infeasible();
      if (prune) localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
    }
    if (!prune) {
      std::vector<HighsInt> branchPositions;
      auto domchgStack = localdom.getReducedDomainChangeStack(branchPositions);
      nodequeue.emplaceNode(std::move(domchgStack), std::move(branchPositions),
                            nodestack.back().lower_bound,
                            nodestack.back().estimate, getCurrentDepth());
    } else {
      mipsolver.mipdata_->debugSolution.nodePruned(localdom);
      treeweight += std::pow(0.5, getCurrentDepth() - 1);
    }
    nodestack.back().opensubtrees = 0;
    if (nodestack.back().nodeBasis)
      basis = std::move(nodestack.back().nodeBasis);

    backtrack(false);
  }

  lp->flushDomain(localdom);
  if (basis) {
    if (basis->row_status.size() == lp->numRows())
      lp->setStoredBasis(std::move(basis));
    lp->recoverBasis();
  }
}

void HighsSearch::flushStatistics() {
  mipsolver.mipdata_->num_nodes += nnodes;
  nnodes = 0;

  mipsolver.mipdata_->pruned_treeweight += treeweight;
  treeweight = 0;

  mipsolver.mipdata_->total_lp_iterations += lpiterations;
  lpiterations = 0;

  mipsolver.mipdata_->heuristic_lp_iterations += heurlpiterations;
  heurlpiterations = 0;

  mipsolver.mipdata_->sb_lp_iterations += sblpiterations;
  sblpiterations = 0;
}

int64_t HighsSearch::getHeuristicLpIterations() const {
  return heurlpiterations + mipsolver.mipdata_->heuristic_lp_iterations;
}

int64_t HighsSearch::getTotalLpIterations() const {
  return lpiterations + mipsolver.mipdata_->total_lp_iterations;
}

int64_t HighsSearch::getLocalLpIterations() const { return lpiterations; }

int64_t HighsSearch::getStrongBranchingLpIterations() const {
  return sblpiterations + mipsolver.mipdata_->sb_lp_iterations;
}

void HighsSearch::resetLocalDomain() {
  this->lp->getLpSolver().changeColsBounds(
      0, mipsolver.numCol() - 1, mipsolver.mipdata_->domain.colLower_.data(),
      mipsolver.mipdata_->domain.colUpper_.data());
  localdom = mipsolver.mipdata_->domain;

#ifndef NDEBUG
  for (HighsInt i = 0; i != mipsolver.numCol(); ++i) {
    assert(lp->getLpSolver().getLp().colLower_[i] == localdom.colLower_[i] ||
           mipsolver.variableType(i) == HighsVarType::kContinuous);
    assert(lp->getLpSolver().getLp().colUpper_[i] == localdom.colUpper_[i] ||
           mipsolver.variableType(i) == HighsVarType::kContinuous);
  }
#endif
}

void HighsSearch::installNode(HighsNodeQueue::OpenNode&& node) {
  localdom.setDomainChangeStack(node.domchgstack, node.branchings);
  bool globalSymmetriesValid = true;
  if (mipsolver.mipdata_->globalOrbits) {
    // if global orbits have been computed we check whether they are still valid
    // in this node
    const auto& domchgstack = localdom.getDomainChangeStack();
    const auto& branchpos = localdom.getBranchingPositions();
    for (HighsInt i : localdom.getBranchingPositions()) {
      HighsInt col = domchgstack[i].column;
      if (mipsolver.mipdata_->symmetries.columnPosition[col] == -1) continue;

      if (!mipsolver.mipdata_->domain.isBinary(col) ||
          (domchgstack[i].boundtype == HighsBoundType::kLower &&
           domchgstack[i].boundval == 1.0)) {
        globalSymmetriesValid = false;
        break;
      }
    }
  }
  nodestack.emplace_back(
      node.lower_bound, node.estimate, nullptr,
      globalSymmetriesValid ? mipsolver.mipdata_->globalOrbits : nullptr);
  subrootsol.clear();
  depthoffset = node.depth - 1;
}

HighsSearch::NodeResult HighsSearch::evaluateNode() {
  assert(!nodestack.empty());
  NodeData& currnode = nodestack.back();
  const NodeData* parent = getParentNodeData();

  const auto& domchgstack = localdom.getDomainChangeStack();

  localdom.propagate();

  if (!localdom.infeasible()) {
    if (mipsolver.mipdata_->symmetries.numPerms > 0 &&
        !currnode.stabilizerOrbits &&
        (parent == nullptr || !parent->stabilizerOrbits ||
         !parent->stabilizerOrbits->orbitCols.empty())) {
      currnode.stabilizerOrbits =
          mipsolver.mipdata_->symmetries.computeStabilizerOrbits(localdom);
    }

    if (currnode.stabilizerOrbits)
      currnode.stabilizerOrbits->orbitalFixing(localdom);
  }
  if (parent != nullptr) {
    int64_t inferences = domchgstack.size() - (currnode.domgchgStackPos + 1);
    pseudocost.addInferenceObservation(
        parent->branchingdecision.column, inferences,
        parent->branchingdecision.boundtype == HighsBoundType::kLower);
  }

  NodeResult result = NodeResult::kOpen;

  if (localdom.infeasible()) {
    result = NodeResult::kDomainInfeasible;
    localdom.clearChangedCols();
    if (parent != nullptr && parent->lp_objective != -kHighsInf &&
        parent->branching_point != parent->branchingdecision.boundval) {
      HighsInt col = parent->branchingdecision.column;
      bool upbranch =
          parent->branchingdecision.boundtype == HighsBoundType::kLower;
      pseudocost.addCutoffObservation(col, upbranch);
    }

    localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
  } else {
    lp->flushDomain(localdom);
    lp->setObjectiveLimit(mipsolver.mipdata_->upper_limit);

#ifndef NDEBUG
    for (HighsInt i = 0; i != mipsolver.numCol(); ++i) {
      assert(lp->getLpSolver().getLp().colLower_[i] == localdom.colLower_[i] ||
             mipsolver.variableType(i) == HighsVarType::kContinuous);
      assert(lp->getLpSolver().getLp().colUpper_[i] == localdom.colUpper_[i] ||
             mipsolver.variableType(i) == HighsVarType::kContinuous);
    }
#endif
    int64_t oldnumiters = lp->getNumLpIterations();
    HighsLpRelaxation::Status status = lp->resolveLp(&localdom);
    lpiterations += lp->getNumLpIterations() - oldnumiters;

    if (localdom.infeasible()) {
      result = NodeResult::kDomainInfeasible;
      localdom.clearChangedCols();
      if (parent != nullptr && parent->lp_objective != -kHighsInf &&
          parent->branching_point != parent->branchingdecision.boundval) {
        HighsInt col = parent->branchingdecision.column;
        bool upbranch =
            parent->branchingdecision.boundtype == HighsBoundType::kLower;
        pseudocost.addCutoffObservation(col, upbranch);
      }

      localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
    } else if (lp->scaledOptimal(status)) {
      lp->storeBasis();
      lp->resetAges();

      currnode.nodeBasis = lp->getStoredBasis();
      currnode.estimate = lp->computeBestEstimate(pseudocost);
      currnode.lp_objective = lp->getObjective();

      if (parent != nullptr && parent->lp_objective != -kHighsInf &&
          parent->branching_point != parent->branchingdecision.boundval) {
        HighsInt col = parent->branchingdecision.column;
        double delta =
            parent->branchingdecision.boundval - parent->branching_point;
        double objdelta =
            std::max(0.0, currnode.lp_objective - parent->lp_objective);

        pseudocost.addObservation(col, delta, objdelta);
      }

      if (lp->unscaledPrimalFeasible(status)) {
        if (lp->getFractionalIntegers().empty()) {
          result = NodeResult::kBoundExceeding;
          double cutoffbnd = getCutoffBound();
          mipsolver.mipdata_->addIncumbent(
              lp->getLpSolver().getSolution().col_value, lp->getObjective(),
              inheuristic ? 'H' : 'T');
          if (mipsolver.mipdata_->upper_limit < cutoffbnd)
            lp->setObjectiveLimit(mipsolver.mipdata_->upper_limit);
          addBoundExceedingConflict();
        }
      }

      if (result == NodeResult::kOpen) {
        if (lp->unscaledDualFeasible(status)) {
          currnode.lower_bound =
              std::max(currnode.lp_objective, currnode.lower_bound);

          if (currnode.lower_bound > getCutoffBound()) {
            result = NodeResult::kBoundExceeding;
            addBoundExceedingConflict();
          } else if (mipsolver.mipdata_->upper_limit != kHighsInf) {
            HighsRedcostFixing::propagateRedCost(mipsolver, localdom, *lp);
            if (localdom.infeasible()) {
              result = NodeResult::kBoundExceeding;
              addBoundExceedingConflict();
              localdom.clearChangedCols();
            } else if (!localdom.getChangedCols().empty()) {
              return evaluateNode();
            }
          }
        } else if (lp->getObjective() > getCutoffBound()) {
          // the LP is not solved to dual feasibilty due to scaling/numerics
          // therefore we compute a conflict constraint as if the LP was bound
          // exceeding and propagate the local domain again. The lp relaxation
          // class will take care to consider the dual multipliers with an
          // increased zero tolerance due to the dual infeasibility when
          // computing the proof conBoundExceedingstraint.
          addBoundExceedingConflict();
          localdom.propagate();
          if (localdom.infeasible()) {
            result = NodeResult::kBoundExceeding;
          }
        }
      }
    } else if (status == HighsLpRelaxation::Status::kInfeasible) {
      if (lp->getLpSolver().getModelStatus(true) ==
          HighsModelStatus::kObjectiveBound)
        result = NodeResult::kBoundExceeding;
      else
        result = NodeResult::kLpInfeasible;
      addInfeasibleConflict();
      if (parent != nullptr && parent->lp_objective != -kHighsInf &&
          parent->branching_point != parent->branchingdecision.boundval) {
        HighsInt col = parent->branchingdecision.column;
        bool upbranch =
            parent->branchingdecision.boundtype == HighsBoundType::kLower;
        pseudocost.addCutoffObservation(col, upbranch);
      }
    }
  }

  if (result != NodeResult::kOpen) {
    mipsolver.mipdata_->debugSolution.nodePruned(localdom);
    treeweight += std::pow(0.5, getCurrentDepth() - 1);
    currnode.opensubtrees = 0;
  }

  return result;
}

HighsSearch::NodeResult HighsSearch::branch() {
  assert(localdom.getChangedCols().empty());

  assert(nodestack.back().opensubtrees == 2);
  nodestack.back().branchingdecision.column = -1;
  inbranching = true;

  HighsInt minrel = pseudocost.getMinReliable();

  NodeResult result = NodeResult::kOpen;
  while (nodestack.back().opensubtrees == 2 &&
         lp->scaledOptimal(lp->getStatus()) &&
         !lp->getFractionalIntegers().empty()) {
    int64_t sbmaxiters = 0;
    if (minrel > 0) {
      int64_t sbiters = getStrongBranchingLpIterations();
      sbmaxiters =
          100000 + ((getTotalLpIterations() - getHeuristicLpIterations() -
                     getStrongBranchingLpIterations()) >>
                    1);
      if (sbiters > sbmaxiters) {
        pseudocost.setMinReliable(0);
      } else if (sbiters > sbmaxiters / 2) {
        double reductionratio =
            (sbiters - sbmaxiters / 2) / (double)(sbmaxiters - sbmaxiters / 2);

        HighsInt minrelreduced = int(minrel - reductionratio * (minrel - 1));
        pseudocost.setMinReliable(std::min(minrel, minrelreduced));
      }
    }

    double degeneracyFac = lp->computeLPDegneracy(localdom);
    pseudocost.setDegeneracyFactor(degeneracyFac);
    if (degeneracyFac >= 10.0) pseudocost.setMinReliable(0);
    HighsInt branchcand = selectBranchingCandidate(sbmaxiters);
    NodeData& currnode = nodestack.back();
    if (branchcand != -1) {
      auto branching = lp->getFractionalIntegers()[branchcand];
      currnode.branchingdecision.column = branching.first;
      currnode.branching_point = branching.second;

      HighsInt col = branching.first;
      switch (childselrule) {
        case ChildSelectionRule::kUp:
          currnode.branchingdecision.boundtype = HighsBoundType::kLower;
          currnode.branchingdecision.boundval =
              std::ceil(currnode.branching_point);
          break;
        case ChildSelectionRule::kDown:
          currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
          currnode.branchingdecision.boundval =
              std::floor(currnode.branching_point);
          break;
        case ChildSelectionRule::kRootSol: {
          double downPrio = pseudocost.getAvgInferencesDown(col) +
                            mipsolver.mipdata_->epsilon;
          double upPrio =
              pseudocost.getAvgInferencesUp(col) + mipsolver.mipdata_->epsilon;
          double downVal = std::floor(currnode.branching_point);
          double upVal = std::ceil(currnode.branching_point);
          if (!subrootsol.empty()) {
            double rootsol = subrootsol[col];
            if (rootsol < downVal)
              rootsol = downVal;
            else if (rootsol > upVal)
              rootsol = upVal;

            upPrio *= (1.0 + (currnode.branching_point - rootsol));
            downPrio *= (1.0 + (rootsol - currnode.branching_point));

          } else {
            if (currnode.lp_objective != -kHighsInf)
              subrootsol = lp->getSolution().col_value;
            if (!mipsolver.mipdata_->rootlpsol.empty()) {
              double rootsol = mipsolver.mipdata_->rootlpsol[col];
              if (rootsol < downVal)
                rootsol = downVal;
              else if (rootsol > upVal)
                rootsol = upVal;

              upPrio *= (1.0 + (currnode.branching_point - rootsol));
              downPrio *= (1.0 + (rootsol - currnode.branching_point));
            }
          }
          if (upPrio + mipsolver.mipdata_->epsilon >= downPrio) {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval = upVal;
          } else {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval = downVal;
          }
          break;
        }
        case ChildSelectionRule::kObj:
          if (mipsolver.colCost(col) >= 0) {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval =
                std::ceil(currnode.branching_point);
          } else {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval =
                std::floor(currnode.branching_point);
          }
          break;
        case ChildSelectionRule::kRandom:
          if (random.bit()) {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval =
                std::ceil(currnode.branching_point);
          } else {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval =
                std::floor(currnode.branching_point);
          }
          break;
        case ChildSelectionRule::kBestCost: {
          if (pseudocost.getPseudocostUp(col, currnode.branching_point,
                                         mipsolver.mipdata_->feastol) >
              pseudocost.getPseudocostDown(col, currnode.branching_point,
                                           mipsolver.mipdata_->feastol)) {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval =
                std::floor(currnode.branching_point);
          } else {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval =
                std::ceil(currnode.branching_point);
          }
          break;
        }
        case ChildSelectionRule::kWorstCost:
          if (pseudocost.getPseudocostUp(col, currnode.branching_point) >=
              pseudocost.getPseudocostDown(col, currnode.branching_point)) {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval =
                std::ceil(currnode.branching_point);
          } else {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval =
                std::floor(currnode.branching_point);
          }
          break;
        case ChildSelectionRule::kDisjunction: {
          int64_t numnodesup;
          int64_t numnodesdown;
          numnodesup = mipsolver.mipdata_->nodequeue.numNodesUp(col);
          numnodesdown = mipsolver.mipdata_->nodequeue.numNodesDown(col);
          if (numnodesup > numnodesdown) {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval =
                std::ceil(currnode.branching_point);
          } else if (numnodesdown > numnodesup) {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval =
                std::floor(currnode.branching_point);
          } else {
            if (mipsolver.colCost(col) >= 0) {
              currnode.branchingdecision.boundtype = HighsBoundType::kLower;
              currnode.branchingdecision.boundval =
                  std::ceil(currnode.branching_point);
            } else {
              currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
              currnode.branchingdecision.boundval =
                  std::floor(currnode.branching_point);
            }
          }
          break;
        }
        case ChildSelectionRule::kHybridInferenceCost: {
          double upVal = std::ceil(currnode.branching_point);
          double downVal = std::floor(currnode.branching_point);
          double upScore =
              (1 + pseudocost.getAvgInferencesUp(col)) /
              pseudocost.getPseudocostUp(col, currnode.branching_point,
                                         mipsolver.mipdata_->feastol);
          double downScore =
              (1 + pseudocost.getAvgInferencesDown(col)) /
              pseudocost.getPseudocostDown(col, currnode.branching_point,
                                           mipsolver.mipdata_->feastol);

          if (upScore >= downScore) {
            currnode.branchingdecision.boundtype = HighsBoundType::kLower;
            currnode.branchingdecision.boundval = upVal;
          } else {
            currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
            currnode.branchingdecision.boundval = downVal;
          }
        }
      }
      result = NodeResult::kBranched;
      break;
    }

    assert(!localdom.getChangedCols().empty());
    result = evaluateNode();
  }
  inbranching = false;
  NodeData& currnode = nodestack.back();
  pseudocost.setMinReliable(minrel);
  pseudocost.setDegeneracyFactor(1.0);

  assert(currnode.opensubtrees == 2 || currnode.opensubtrees == 0);

  if (currnode.opensubtrees != 2) return result;

  if (currnode.branchingdecision.column == -1) {
    double bestscore = -1.0;
    // solution branching failed, so choose any integer variable to branch
    // on in case we have a different solution status could happen due to a
    // fail in the LP solution process

    for (HighsInt i : mipsolver.mipdata_->integral_cols) {
      if (localdom.colUpper_[i] - localdom.colLower_[i] < 0.5) continue;

      double fracval;
      if (localdom.colLower_[i] != -kHighsInf &&
          localdom.colUpper_[i] != kHighsInf)
        fracval = std::floor(0.5 * (localdom.colLower_[i] +
                                    localdom.colUpper_[i] + 0.5)) +
                  0.5;
      if (localdom.colLower_[i] != -kHighsInf)
        fracval = localdom.colLower_[i] + 0.5;
      else if (localdom.colUpper_[i] != kHighsInf)
        fracval = localdom.colUpper_[i] - 0.5;
      else
        fracval = 0.5;

      double score = pseudocost.getScore(i, fracval);
      assert(score >= 0.0);

      if (score > bestscore) {
        bestscore = score;
        if (mipsolver.colCost(i) >= 0) {
          double upval = std::ceil(fracval);
          currnode.branching_point = upval;
          currnode.branchingdecision.boundtype = HighsBoundType::kLower;
          currnode.branchingdecision.column = i;
          currnode.branchingdecision.boundval = upval;
        } else {
          double downval = std::floor(fracval);
          currnode.branching_point = downval;
          currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
          currnode.branchingdecision.column = i;
          currnode.branchingdecision.boundval = downval;
        }
      }
    }
  }

  if (currnode.branchingdecision.column == -1) {
    lp->setIterationLimit();

    // create a fresh LP only with model rows since all integer columns are
    // fixed, the cutting planes are not required and the LP could not be solved
    // so we want to make it as easy as possible
    HighsLpRelaxation lpCopy(mipsolver);
    lpCopy.loadModel();
    lpCopy.getLpSolver().changeColsBounds(0, mipsolver.numCol() - 1,
                                          localdom.colLower_.data(),
                                          localdom.colUpper_.data());
    // temporarily use the fresh LP for the HighsSearch class
    HighsLpRelaxation* tmpLp = &lpCopy;
    std::swap(tmpLp, lp);

    // reevaluate the node with LP presolve enabled
    lp->getLpSolver().setOptionValue("presolve", "on");
    result = evaluateNode();

    if (result == NodeResult::kOpen) {
      // LP still not solved, reevaluate with primal simplex
      lp->getLpSolver().clearSolver();
      lp->getLpSolver().setOptionValue("simplex_strategy",
                                       kSimplexStrategyPrimal);
      result = evaluateNode();
      lp->getLpSolver().setOptionValue("simplex_strategy",
                                       kSimplexStrategyDual);
      if (result == NodeResult::kOpen) {
        // LP still not solved, reevaluate with IPM instead of simplex
        lp->getLpSolver().clearSolver();
        lp->getLpSolver().setOptionValue("solver", "ipm");
        result = evaluateNode();

        if (result == NodeResult::kOpen) {
          highsLogUser(mipsolver.options_mip_->log_options,
                       HighsLogType::kWarning,
                       "Failed to solve node with all integer columns "
                       "fixed. Declaring node infeasible.\n");
          // LP still not solved, give up and declare as infeasible
          currnode.opensubtrees = 0;
          result = NodeResult::kLpInfeasible;
        }
      }
    }

    // restore old lp relaxation
    std::swap(tmpLp, lp);

    return result;
  }

  // finally open a new node with the branching decision added
  // and remember that we have one open subtree left
  HighsInt domchgPos = localdom.getDomainChangeStack().size();

  bool passStabilizerToChildNode =
      orbitsValidInChildNode(currnode.branchingdecision);
  localdom.changeBound(currnode.branchingdecision);
  currnode.opensubtrees = 1;

  nodestack.emplace_back(
      currnode.lower_bound, currnode.estimate, currnode.nodeBasis,
      passStabilizerToChildNode ? currnode.stabilizerOrbits : nullptr);
  nodestack.back().domgchgStackPos = domchgPos;

  return NodeResult::kBranched;
}

bool HighsSearch::backtrack(bool recoverBasis) {
  if (nodestack.empty()) return false;
  assert(!nodestack.empty());
  assert(nodestack.back().opensubtrees == 0);

  while (true) {
    while (nodestack.back().opensubtrees == 0) {
      depthoffset += nodestack.back().skipDepthCount;
      nodestack.pop_back();

      if (nodestack.empty()) {
        localdom.backtrackToGlobal();
        lp->flushDomain(localdom);
        return false;
      }

#ifndef NDEBUG
      HighsDomainChange branchchg =
#endif
          localdom.backtrack();

      if (nodestack.back().opensubtrees != 0) {
        // repropagate the node, as it may have become infeasible due to
        // conflicts
        HighsInt oldNumDomchgs = localdom.getNumDomainChanges();
        HighsInt oldNumChangedCols = localdom.getChangedCols().size();
        localdom.propagate();
        if (nodestack.back().stabilizerOrbits && !localdom.infeasible() &&
            oldNumDomchgs != localdom.getNumDomainChanges()) {
          nodestack.back().stabilizerOrbits->orbitalFixing(localdom);
        }
        if (localdom.infeasible()) {
          localdom.clearChangedCols(oldNumChangedCols);
          nodestack.back().opensubtrees = 0;
        }
      }

      assert(
          (branchchg.boundtype == HighsBoundType::kLower &&
           branchchg.boundval >= nodestack.back().branchingdecision.boundval) ||
          (branchchg.boundtype == HighsBoundType::kUpper &&
           branchchg.boundval <= nodestack.back().branchingdecision.boundval));
      assert(branchchg.boundtype ==
             nodestack.back().branchingdecision.boundtype);
      assert(branchchg.column == nodestack.back().branchingdecision.column);
    }

    NodeData& currnode = nodestack.back();

    assert(currnode.opensubtrees == 1);
    currnode.opensubtrees = 0;
    bool fallbackbranch =
        currnode.branchingdecision.boundval == currnode.branching_point;
    HighsInt domchgPos = localdom.getDomainChangeStack().size();
    if (currnode.branchingdecision.boundtype == HighsBoundType::kLower) {
      currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
      currnode.branchingdecision.boundval =
          std::floor(currnode.branchingdecision.boundval - 0.5);
    } else {
      currnode.branchingdecision.boundtype = HighsBoundType::kLower;
      currnode.branchingdecision.boundval =
          std::ceil(currnode.branchingdecision.boundval + 0.5);
    }

    if (fallbackbranch)
      currnode.branching_point = currnode.branchingdecision.boundval;

    HighsInt numChangedCols = localdom.getChangedCols().size();
    bool passStabilizerToChildNode =
        orbitsValidInChildNode(currnode.branchingdecision);
    localdom.changeBound(currnode.branchingdecision);
    bool prune = nodestack.back().lower_bound > getCutoffBound() ||
                 localdom.infeasible();
    if (!prune) {
      localdom.propagate();
      prune = localdom.infeasible();
      if (prune) localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
    }
    if (!prune && passStabilizerToChildNode && currnode.stabilizerOrbits) {
      currnode.stabilizerOrbits->orbitalFixing(localdom);
      prune = localdom.infeasible();
    }
    if (prune) {
      localdom.backtrack();
      localdom.clearChangedCols(numChangedCols);
      treeweight += std::pow(0.5, getCurrentDepth());
      continue;
    }
    nodestack.emplace_back(
        currnode.lower_bound, currnode.estimate, currnode.nodeBasis,
        passStabilizerToChildNode ? currnode.stabilizerOrbits : nullptr);

    lp->flushDomain(localdom);
    nodestack.back().domgchgStackPos = domchgPos;
    break;
  }

  if (recoverBasis && nodestack.back().nodeBasis) {
    lp->setStoredBasis(nodestack.back().nodeBasis);
    lp->recoverBasis();
  }

  return true;
}

bool HighsSearch::backtrackPlunge(HighsNodeQueue& nodequeue) {
  const std::vector<HighsDomainChange>& domchgstack =
      localdom.getDomainChangeStack();

  if (nodestack.empty()) return false;
  assert(!nodestack.empty());
  assert(nodestack.back().opensubtrees == 0);

  while (true) {
    while (nodestack.back().opensubtrees == 0) {
      depthoffset += nodestack.back().skipDepthCount;
      nodestack.pop_back();

      if (nodestack.empty()) {
        localdom.backtrackToGlobal();
        lp->flushDomain(localdom);
        return false;
      }
#ifndef NDEBUG
      HighsDomainChange branchchg =
#endif
          localdom.backtrack();

      if (nodestack.back().opensubtrees != 0) {
        // repropagate the node, as it may have become infeasible due to
        // conflicts
        HighsInt oldNumDomchgs = localdom.getNumDomainChanges();
        HighsInt oldNumChangedCols = localdom.getChangedCols().size();
        localdom.propagate();
        if (nodestack.back().stabilizerOrbits && !localdom.infeasible() &&
            oldNumDomchgs != localdom.getNumDomainChanges()) {
          nodestack.back().stabilizerOrbits->orbitalFixing(localdom);
        }
        if (localdom.infeasible()) {
          localdom.clearChangedCols(oldNumChangedCols);
          nodestack.back().opensubtrees = 0;
        }
      }

      assert(
          (branchchg.boundtype == HighsBoundType::kLower &&
           branchchg.boundval >= nodestack.back().branchingdecision.boundval) ||
          (branchchg.boundtype == HighsBoundType::kUpper &&
           branchchg.boundval <= nodestack.back().branchingdecision.boundval));
      assert(branchchg.boundtype ==
             nodestack.back().branchingdecision.boundtype);
      assert(branchchg.column == nodestack.back().branchingdecision.column);
    }

    NodeData& currnode = nodestack.back();

    assert(currnode.opensubtrees == 1);
    currnode.opensubtrees = 0;
    bool fallbackbranch =
        currnode.branchingdecision.boundval == currnode.branching_point;
    double nodeScore;
    if (currnode.branchingdecision.boundtype == HighsBoundType::kLower) {
      currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
      currnode.branchingdecision.boundval =
          std::floor(currnode.branchingdecision.boundval - 0.5);
      nodeScore = pseudocost.getScoreDown(
          currnode.branchingdecision.column,
          fallbackbranch ? 0.5 : currnode.branching_point);
    } else {
      currnode.branchingdecision.boundtype = HighsBoundType::kLower;
      currnode.branchingdecision.boundval =
          std::ceil(currnode.branchingdecision.boundval + 0.5);
      nodeScore = pseudocost.getScoreUp(
          currnode.branchingdecision.column,
          fallbackbranch ? 0.5 : currnode.branching_point);
    }

    if (fallbackbranch)
      currnode.branching_point = currnode.branchingdecision.boundval;

    HighsInt domchgPos = domchgstack.size();
    HighsInt numChangedCols = localdom.getChangedCols().size();
    bool passStabilizerToChildNode =
        orbitsValidInChildNode(currnode.branchingdecision);
    localdom.changeBound(currnode.branchingdecision);
    bool prune = nodestack.back().lower_bound > getCutoffBound() ||
                 localdom.infeasible();
    if (!prune) {
      localdom.propagate();
      prune = localdom.infeasible();
      if (prune) localdom.conflictAnalysis(mipsolver.mipdata_->conflictPool);
    }
    if (!prune && passStabilizerToChildNode && currnode.stabilizerOrbits) {
      currnode.stabilizerOrbits->orbitalFixing(localdom);
      prune = localdom.infeasible();
    }
    if (prune) {
      localdom.backtrack();
      localdom.clearChangedCols(numChangedCols);
      treeweight += std::pow(0.5, getCurrentDepth());
      continue;
    }
    bool nodeToQueue = false;
    // we check if switching to the other branch of an anchestor yields a higher
    // additive branch score than staying in this node and if so we postpone the
    // node and put it to the queue to backtrack further.
    for (HighsInt i = nodestack.size() - 2; i >= 0; --i) {
      if (nodestack[i].opensubtrees == 0) continue;

      bool fallbackbranch = nodestack[i].branchingdecision.boundval ==
                            nodestack[i].branching_point;
      double branchpoint = fallbackbranch ? 0.5 : nodestack[i].branching_point;
      double ancestorScoreActive;
      double ancestorScoreInactive;
      if (nodestack[i].branchingdecision.boundtype == HighsBoundType::kLower) {
        ancestorScoreInactive = pseudocost.getScoreDown(
            nodestack[i].branchingdecision.column, branchpoint);
        ancestorScoreActive = pseudocost.getScoreUp(
            nodestack[i].branchingdecision.column, branchpoint);
      } else {
        ancestorScoreActive = pseudocost.getScoreDown(
            nodestack[i].branchingdecision.column, branchpoint);
        ancestorScoreInactive = pseudocost.getScoreUp(
            nodestack[i].branchingdecision.column, branchpoint);
      }

      // if (!mipsolver.submip)
      //   printf("nodeScore: %g, ancestorScore: %g\n", nodeScore,
      //   ancestorScore);
      nodeToQueue = ancestorScoreInactive - ancestorScoreActive >
                    nodeScore + mipsolver.mipdata_->feastol;
      break;
    }
    if (nodeToQueue) {
      // if (!mipsolver.submip) printf("node goes to queue\n");
      localdom.backtrack();
      localdom.clearChangedCols(numChangedCols);
      std::vector<HighsInt> branchPositions;
      auto domchgStack = localdom.getReducedDomainChangeStack(branchPositions);
      nodequeue.emplaceNode(std::move(domchgStack), std::move(branchPositions),
                            nodestack.back().lower_bound,
                            nodestack.back().estimate, getCurrentDepth() + 1);
      continue;
    }
    nodestack.emplace_back(
        currnode.lower_bound, currnode.estimate, currnode.nodeBasis,
        passStabilizerToChildNode ? currnode.stabilizerOrbits : nullptr);

    lp->flushDomain(localdom);
    nodestack.back().domgchgStackPos = domchgPos;
    break;
  }

  if (nodestack.back().nodeBasis) {
    lp->setStoredBasis(nodestack.back().nodeBasis);
    lp->recoverBasis();
  }

  return true;
}

bool HighsSearch::backtrackUntilDepth(HighsInt targetDepth) {
  if (nodestack.empty()) return false;
  assert(!nodestack.empty());
  if (getCurrentDepth() >= targetDepth) nodestack.back().opensubtrees = 0;

  while (nodestack.back().opensubtrees == 0) {
    depthoffset += nodestack.back().skipDepthCount;
    nodestack.pop_back();

#ifndef NDEBUG
    HighsDomainChange branchchg =
#endif
        localdom.backtrack();
    if (nodestack.empty()) {
      lp->flushDomain(localdom);
      return false;
    }
    assert(
        (branchchg.boundtype == HighsBoundType::kLower &&
         branchchg.boundval >= nodestack.back().branchingdecision.boundval) ||
        (branchchg.boundtype == HighsBoundType::kUpper &&
         branchchg.boundval <= nodestack.back().branchingdecision.boundval));
    assert(branchchg.boundtype == nodestack.back().branchingdecision.boundtype);
    assert(branchchg.column == nodestack.back().branchingdecision.column);

    if (getCurrentDepth() >= targetDepth) nodestack.back().opensubtrees = 0;
  }

  NodeData& currnode = nodestack.back();
  assert(currnode.opensubtrees == 1);
  currnode.opensubtrees = 0;
  bool fallbackbranch =
      currnode.branchingdecision.boundval == currnode.branching_point;
  if (currnode.branchingdecision.boundtype == HighsBoundType::kLower) {
    currnode.branchingdecision.boundtype = HighsBoundType::kUpper;
    currnode.branchingdecision.boundval =
        std::floor(currnode.branchingdecision.boundval - 0.5);
  } else {
    currnode.branchingdecision.boundtype = HighsBoundType::kLower;
    currnode.branchingdecision.boundval =
        std::ceil(currnode.branchingdecision.boundval + 0.5);
  }

  if (fallbackbranch)
    currnode.branching_point = currnode.branchingdecision.boundval;

  HighsInt domchgPos = localdom.getDomainChangeStack().size();
  bool passStabilizerToChildNode =
      orbitsValidInChildNode(currnode.branchingdecision);
  localdom.changeBound(currnode.branchingdecision);
  nodestack.emplace_back(
      currnode.lower_bound, currnode.estimate, currnode.nodeBasis,
      passStabilizerToChildNode ? currnode.stabilizerOrbits : nullptr);

  lp->flushDomain(localdom);
  nodestack.back().domgchgStackPos = domchgPos;
  if (nodestack.back().nodeBasis &&
      nodestack.back().nodeBasis->row_status.size() == lp->getLp().numRow_)
    lp->setStoredBasis(nodestack.back().nodeBasis);
  lp->recoverBasis();

  return true;
}

HighsSearch::NodeResult HighsSearch::dive() {
  reliableatnode.clear();

  do {
    ++nnodes;
    NodeResult result = evaluateNode();

    if (mipsolver.mipdata_->checkLimits()) return result;

    if (result != NodeResult::kOpen) return result;

    result = branch();
    if (result != NodeResult::kBranched) return result;
  } while (true);
}

void HighsSearch::solveDepthFirst(int64_t maxbacktracks) {
  do {
    if (maxbacktracks == 0) break;

    NodeResult result = dive();
    // if a limit was reached the result might be open
    if (result == NodeResult::kOpen) break;

    --maxbacktracks;

  } while (backtrack());
}