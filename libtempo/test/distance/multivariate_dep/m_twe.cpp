#define CATCH_CONFIG_FAST_COMPILE

#include <catch.hpp>
#include <libtempo/distance/twe.hpp>
#include <iostream>

#include "../mock/mockseries.hpp"

using namespace mock;
using namespace libtempo::distance;
constexpr size_t nbitems = 500;
constexpr size_t ndim = 3;

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Reference
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
namespace {

  using namespace libtempo::utils;
  using namespace std;

  /// Naive Univariate TWE. Reference code.
  double twe_matrix_uni(const vector<double>& s1, const vector<double>& s2, double nu, double lambda) {
    const size_t length1 = s1.size();
    const size_t length2 = s2.size();

    // Check lengths. Be explicit in the conditions.
    if (length1==0 && length2==0) { return 0; }
    if (length1==0 && length2!=0) { return PINF<double>; }
    if (length1!=0 && length2==0) { return PINF<double>; }

    const size_t maxLength = max(length1, length2);
    vector<std::vector<double>> matrix(maxLength, std::vector<double>(maxLength, PINF<double>));

    const double nu_lambda = nu+lambda;
    const double nu2 = 2*nu;

    // Initialization: first cell, first column and first row
    matrix[0][0] = sqdist(s1[0], s2[0]);
    for (size_t i = 1; i<length1; i++) { matrix[i][0] = matrix[i-1][0]+sqdist(s1[i], s1[i-1])+nu_lambda; }
    for (size_t j = 1; j<length2; j++) { matrix[0][j] = matrix[0][j-1]+sqdist(s2[j], s2[j-1])+nu_lambda; }

    // Main Loop
    for (size_t i = 1; i<length1; i++) {
      for (size_t j = 1; j<length2; j++) {
        // Top: over the lines
        double t = matrix[i-1][j]+sqdist(s1[i], s1[i-1])+nu_lambda;
        // Diagonal
        double d = matrix[i-1][j-1]+sqdist(s1[i], s2[j])+sqdist(s1[i-1], s2[j-1])+nu2*absdiff(i, j);
        // Previous: over the columns
        double p = matrix[i][j-1]+sqdist(s2[j], s2[j-1])+nu_lambda;
        //
        matrix[i][j] = min(t, d, p);
      }
    }

    // Output
    return matrix[length1-1][length2-1];
  }

  /// Naive Multivariate TWE with a window. Reference code.
  double twe_matrix(const vector<double>& s1, const vector<double>& s2, size_t dim, double nu, double lambda) {
    const size_t length1 = s1.size()/dim;
    const size_t length2 = s2.size()/dim;

    // Check lengths. Be explicit in the conditions.
    if (length1==0 && length2==0) { return 0; }
    if (length1==0 && length2!=0) { return PINF<double>; }
    if (length1!=0 && length2==0) { return PINF<double>; }

    const size_t maxLength = max(length1, length2);
    vector<std::vector<double>> matrix(maxLength, std::vector<double>(maxLength, PINF<double>));

    const double nu_lambda = nu+lambda;
    const double nu2 = 2*nu;

    // Initialization: first cell, first column and first row
    matrix[0][0] = sqedN(s1, 0, s2, 0, dim);
    for (size_t i = 1; i<length1; i++) { matrix[i][0] = matrix[i-1][0]+sqedN(s1, i, s1, i-1, dim)+nu_lambda; }
    for (size_t j = 1; j<length2; j++) { matrix[0][j] = matrix[0][j-1]+sqedN(s2, j, s2, j-1, dim)+nu_lambda; }

    // Main Loop
    for (size_t i = 1; i<length1; i++) {
      for (size_t j = 1; j<length2; j++) {
        // Top: over the lines
        double t = matrix[i-1][j]+sqedN(s1, i, s1, i-1, dim)+nu_lambda;
        // Diagonal
        double d = matrix[i-1][j-1]+sqedN(s1, i, s2, j, dim)
          +sqedN(s1, i-1, s2, j-1, dim)+nu2*absdiff(i, j);
        // Previous: over the columns
        double p = matrix[i][j-1]+sqedN(s2, j, s2, j-1, dim)+nu_lambda;
        //
        matrix[i][j] = min(t, d, p);
      }
    }

    // Output
    return matrix[length1-1][length2-1];
  }
}

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Testing
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

TEST_CASE("Multivariate Dependent TWE Fixed length", "[twe][multivariate]") {
  // Setup univariate with fixed length
  Mocker mocker;
  mocker._dim = ndim;
  const auto& nus = mocker.twe_nus;
  const auto& lambdas = mocker.twe_lambdas;
  const auto fset = mocker.vec_randvec(nbitems);

  SECTION("TWE(s,s) == 0") {
    for (const auto& s: fset) {
      for (auto nu: nus) {
        for (auto la: lambdas) {
          const double twe_ref_v = twe_matrix(s, s, ndim, nu, la);
          REQUIRE(twe_ref_v==0);
          const auto twe_v = twe<double>(s, s, ndim, nu, la);
          REQUIRE(twe_v==0);
        }
      }
    }
  }

  SECTION("TWE(s1, s2)") {
    for (size_t i = 0; i<nbitems-1; ++i) {
      const auto& s1 = fset[i];
      const auto& s2 = fset[i+1];
      for (auto nu: nus) {
        for (auto la: lambdas) {
          // Check Uni
          {
            const double twe_ref_v = twe_matrix(s1, s2, 1, nu, la);
            const double twe_ref_uni_v = twe_matrix_uni(s1, s2, nu, la);
            const auto twe_tempo_v = twe<double>(s1, s2, (size_t) 1, nu, la);
            REQUIRE(twe_ref_v==twe_ref_uni_v);
            REQUIRE(twe_ref_v==twe_tempo_v);
          }
          // Check Multi
          {
            const double twe_ref_v = twe_matrix(s1, s2, ndim, nu, la);
            INFO("Exact same operation order. Expect exact floating point equality.")
            const auto twe_tempo_v = twe<double>(s1, s2, ndim, nu, la);
            REQUIRE(twe_ref_v==twe_tempo_v);
          }
        }
      }
    }
  }

  SECTION("NN1 TWE") {
    // Query loop
    for (size_t i = 0; i<nbitems; i += 3) {
      const auto& s1 = fset[i];
      // Ref Variables
      size_t idx_ref = 0;
      double bsf_ref = lu::PINF<double>;
      // Base Variables
      size_t idx = 0;
      double bsf = lu::PINF<double>;
      // EAP Variables
      size_t idx_tempo = 0;
      double bsf_tempo = lu::PINF<double>;

      // NN1 loop
      for (size_t j = 0; j<nbitems; j += 5) {
        // Skip self.
        if (i==j) { continue; }
        const auto& s2 = fset[j];

        for (auto nu: nus) {
          for (auto la: lambdas) {
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const double v_ref = twe_matrix(s1, s2, ndim, nu, la);
            if (v_ref<bsf_ref) {
              idx_ref = j;
              bsf_ref = v_ref;
            }
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v = twe<double>(s1, s2, ndim, nu, la);
            if (v<bsf) {
              idx = j;
              bsf = v;
            }
            REQUIRE(idx_ref==idx);
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v_tempo = twe<double>(s1, s2, ndim, nu, la, bsf_tempo);
            if (v_tempo<bsf_tempo) {
              idx_tempo = j;
              bsf_tempo = v_tempo;
            }
            REQUIRE(idx_ref==idx_tempo);
          }
        }
      }
    }// End query loop
  }// End section
}

TEST_CASE("Multivariate Dependent TWE Variable length", "[twe][multivariate]") {
  // Setup univariate dataset with varying length
  Mocker mocker;
  const auto& nus = mocker.twe_nus;
  const auto& lambdas = mocker.twe_lambdas;
  const auto fset = mocker.vec_rs_randvec(nbitems);

  SECTION("TWE(s,s) == 0") {
    for (const auto& s: fset) {
      for (auto nu: nus) {
        for (auto la: lambdas) {
          const double twe_ref_v = twe_matrix(s, s, ndim, nu, la);
          REQUIRE(twe_ref_v==0);
          const auto twe_v = twe<double>(s, s, ndim, nu, la);
          REQUIRE(twe_v==0);
        }
      }
    }
  }

  SECTION("TWE(s1, s2)") {
    for (size_t i = 0; i<nbitems-1; ++i) {
      const auto& s1 = fset[i];
      const auto& s2 = fset[i+1];

      for (auto nu: nus) {
        for (auto la: lambdas) {
          // Check Uni
          {
            const double twe_ref_v = twe_matrix(s1, s2, 1, nu, la);
            const double twe_ref_uni_v = twe_matrix_uni(s1, s2, nu, la);
            const auto twe_tempo_v = twe<double>(s1, s2, (size_t) 1, nu, la);
            REQUIRE(twe_ref_v==twe_ref_uni_v);
            REQUIRE(twe_ref_v==twe_tempo_v);
          }
          // Check Multi
          {
            const double twe_ref_v = twe_matrix(s1, s2, ndim, nu, la);
            INFO("Exact same operation order. Expect exact floating point equality.")
            const auto twe_tempo_v = twe<double>(s1, s2, ndim, nu, la);
            REQUIRE(twe_ref_v==twe_tempo_v);
          }
        }
      }
    }
  }

  SECTION("NN1 TWE") {
    // Query loop
    for (size_t i = 0; i<nbitems; i += 3) {
      const auto& s1 = fset[i];
      // Ref Variables
      size_t idx_ref = 0;
      double bsf_ref = lu::PINF<double>;
      // Base Variables
      size_t idx = 0;
      double bsf = lu::PINF<double>;
      // EAP Variables
      size_t idx_tempo = 0;
      double bsf_tempo = lu::PINF<double>;

      // NN1 loop
      for (size_t j = 0; j<nbitems; j += 5) {
        // Skip self.
        if (i==j) { continue; }
        const auto& s2 = fset[j];
        for (auto nu: nus) {
          for (auto la: lambdas) {
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const double v_ref = twe_matrix(s1, s2, ndim, nu, la);
            if (v_ref<bsf_ref) {
              idx_ref = j;
              bsf_ref = v_ref;
            }
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v = twe<double>(s1, s2, ndim, nu, la);
            if (v<bsf) {
              idx = j;
              bsf = v;
            }
            REQUIRE(idx_ref==idx);
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v_tempo = twe<double>(s1, s2, ndim, nu, la, bsf_tempo);
            if (v_tempo<bsf_tempo) {
              idx_tempo = j;
              bsf_tempo = v_tempo;
            }
            REQUIRE(idx_ref==idx_tempo);
          }
        }
      }
    }// End query loop
  }// End section

}