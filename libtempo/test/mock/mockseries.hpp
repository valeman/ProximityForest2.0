#pragma once

#include <random>
#include <optional>

#include <libtempo/utils/utils.hpp>

namespace mock {
  using namespace std;
  namespace lu=libtempo::utils;

  template<typename FloatType=double>
  FloatType square_dist(FloatType a, FloatType b){
    FloatType d = a-b;
    return d*d;
  }

  /// Mocker class - init with a
  template<typename FloatType=double, typename LabelType=std::string, typename PRNG= mt19937_64>
  struct Mocker {

    // --- --- --- Fields, open for configuration

    // Random number generator - should be init in the constructor
    unsigned int _seed;
    PRNG _prng;
    // Dimension of the series
    size_t _dim{1};
    // Length of the series
    size_t _minl{20};   // variable, min
    size_t _maxl{30};   // variable, max
    size_t _fixl{25};   // fixed
    // Possible values of the series
    FloatType _minv{0};
    FloatType _maxv{1};

    // Parameters
    std::vector<double> wratios{0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};

    // --- --- --- Constructor

    /** Build a mocker with a random seed. If none is given, one is generated */
    explicit Mocker(std::optional<unsigned int> seed = {}) {
      static_assert(std::is_floating_point_v<FloatType>);
      if (seed.has_value()) {
        _seed = seed.value();
      } else {
        std::random_device r;
        _seed = r();
      }
      _prng = PRNG(_seed);
    }


    // --- --- --- Methods

    /** Random size between min and max */
    [[nodiscard]] inline size_t get_size(size_t minl, size_t maxl) {
      auto dist = std::uniform_int_distribution<std::size_t>(minl, maxl);
      return dist(_prng);
    }

    /** Generate a vector of a given size*_dim with random real values in the half-closed interval [_minv, _maxv[. */
    [[nodiscard]] std::vector<FloatType> randvec(size_t size){
      std::uniform_real_distribution<FloatType> udist{_minv, _maxv};
      auto generator = [this, &udist]() { return udist(_prng); };
      std::vector<FloatType> v(size*_dim);
      std::generate(v.begin(), v.end(), generator);
      return v;
    }

    /** Generate a vector of _fixl size with random real values in the half-closed interval [_minv, _maxv[.*/
    [[nodiscard]] std::vector<FloatType> randvec(){return randvec(_fixl); }

    /** Generate a dataset of fixed length series with nbitems, with values in [_minv, _maxv[ */
    [[nodiscard]] vector<vector<double>> vec_randvec(size_t nbitems) {
      vector<vector<double>> set;
      for (size_t i = 0; i < nbitems; ++i) {
        auto series = randvec(_fixl);
        assert(series.data() != nullptr);
        set.push_back(std::move(series));
      }
      return set;
    }

    /** Generate a vector of a random size between [_minl, _maxl]
     * with random real values in the half-closed interval [_minv, _maxv[.*/
    [[nodiscard]] std::vector<FloatType> rs_randvec(){
      size_t l = get_size(_minl, _maxl);
      return randvec(l);
    }

    /** Generate a dataset of variable length series with nbtimes, with values in [_minv, _maxv[ */
    [[nodiscard]] vector<vector<double>> vec_rs_randvec(size_t nbitems) {
      vector<vector<double>> set;
      for (size_t i = 0; i < nbitems; ++i) {
        auto series = rs_randvec();
        assert(series.data() != nullptr);
        set.push_back(std::move(series));
      }
      return set;
    }
  };


} // End of namespace mockseries