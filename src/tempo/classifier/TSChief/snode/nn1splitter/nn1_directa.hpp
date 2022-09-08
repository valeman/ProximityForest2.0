#pragma once

#include <tempo/utils/utils.hpp>
#include <tempo/dataset/tseries.hpp>
#include <tempo/distance/tseries.univariate.hpp>

#include "nn1dist_base.hpp"

namespace tempo::classifier::TSChief::snode::nn1splitter {

  struct DA : public BaseDist {
    F cfe;

    DA(std::string tname, F cfe) : BaseDist(std::move(tname)), cfe(cfe) {}

    F eval(const TSeries& t1, const TSeries& t2, F bsf) override {
      return distance::univariate::directa(t1, t2, cfe, bsf);
    }

    std::string get_distance_name() override { return "DA:" + std::to_string(cfe); }
  };

  struct DAGen : public i_GenDist {
    TransformGetter get_transform;
    ExponentGetter get_cfe;

    DAGen(TransformGetter gt, ExponentGetter ge) :
      get_transform(std::move(gt)), get_cfe(std::move(ge)) {}

    std::unique_ptr<i_Dist> generate(TreeState& state, TreeData const& /*data*/, const ByClassMap& /* bcm */) override {
      const std::string tn = get_transform(state);
      const F e = get_cfe(state);
      return std::make_unique<DA>(tn, e);
    }
  };

} // End of namespace tempo::classifier::TSChief::snode::nn1splitter
