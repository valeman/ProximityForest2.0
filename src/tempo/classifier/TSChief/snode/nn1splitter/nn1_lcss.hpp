#pragma once

#include <tempo/utils/utils.hpp>
#include <tempo/dataset/tseries.hpp>
#include <tempo/distance/tseries.univariate.hpp>

#include "nn1dist_base.hpp"

namespace tempo::classifier::TSChief::snode::nn1splitter {

  struct LCSS : public BaseDist {
    F epsilon;
    size_t w;

    LCSS(std::string tname, F epsilon, size_t w) : BaseDist(std::move(tname)), epsilon(epsilon), w(w) {}

    F eval(const TSeries& t1, const TSeries& t2, F bsf) override {
      return distance::univariate::lcss(t1, t2, epsilon, w, bsf);
    }

    std::string get_distance_name() override { return "LCSS:" + std::to_string(epsilon) + ":" + std::to_string(w); }
  };

  struct LCSSGen : public i_GenDist {
    TransformGetter get_transform;
    StatGetter get_epsilon;
    WindowGetter get_win;

    LCSSGen(TransformGetter get_transform, StatGetter get_epsilon, WindowGetter get_win) :
      get_transform(std::move(get_transform)), get_epsilon(std::move(get_epsilon)), get_win(std::move(get_win)) {}

    std::unique_ptr<i_Dist> generate(TreeState& state, TreeData const& data, const ByClassMap& bcm) override {
      const std::string tn = get_transform(state);
      const F epsilon = get_epsilon(state, data, bcm, tn);
      const size_t w = get_win(state, data);
      return std::make_unique<LCSS>(tn, epsilon, w);
    }

  };

} // End of namespace tempo::classifier::TSChief::snode::nn1splitter
