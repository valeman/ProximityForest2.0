#define CATCH_CONFIG_FAST_COMPILE

#include <catch.hpp>
#include <libtempo/distance/erp.hpp>

#include "../mock/mockseries.hpp"

using namespace mock;
using namespace libtempo::distance;
constexpr size_t nbitems = 500;

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Reference
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
namespace reference {

  using namespace libtempo::utils;

  /// Naive DTW with a window. Reference code.
  double erp_matrix(const vector<double>& series1, const vector<double>& series2, double gValue, long w) {
    const long length1 = to_signed(series1.size());
    const long length2 = to_signed(series2.size());

    // Check lengths. Be explicit in the conditions.
    if (length1==0 && length2==0) { return 0; }
    if (length1==0 && length2!=0) { return PINF<double>; }
    if (length1!=0 && length2==0) { return PINF<double>; }

    // We will only allocate a double-row buffer: use the smallest possible dimension as the columns.
    const vector<double>& cols = (length1<length2) ? series1 : series2;
    const vector<double>& lines = (length1<length2) ? series2 : series1;
    const long nbcols = min(length1, length2);
    const long nblines = max(length1, length2);

    // Cap the windows
    if (w>nblines) { w = nblines; }

    // Check if, given the constralong w, we can have an alignment.
    if (nblines-nbcols>w) { return PINF<double>; }

    // Allocate a double buffer for the columns. Declare the index of the 'c'urrent and 'p'revious buffer.
    // Note: we use a vector as a way to initialize the buffer with PINF<double>
    vector<std::vector<double>> matrix(nblines+1, std::vector<double>(nbcols+1, PINF<double>));

    // Initialisation of the first line and column
    matrix[0][0] = 0;
    for (long j{1}; j<nbcols+1; j++) {
      matrix[0][j] = matrix[0][j-1]+square_dist(gValue, cols[j-1]);
    }
    for (long i{1}; i<nblines+1; i++) {
      matrix[i][0] = matrix[i-1][0]+square_dist(lines[i-1], gValue);
    }

    // Iterate over the lines
    for (long i{1}; i<nblines+1; ++i) {
      const double li = lines[i-1];
      long l = max<long>(i-w, 1);
      long r = min<long>(i+w+1, nbcols+1);

      // Iterate through the rest of the columns
      for (long j{l}; j<r; ++j) {
        matrix[i][j] = min(
          matrix[i][j-1]+square_dist(gValue, cols[j-1]),        // Previous
          min(matrix[i-1][j-1]+square_dist(li, cols[j-1]),    // Diagonal
            matrix[i-1][j]+square_dist(li, gValue)              // Above
          )
        );
      }
    } // End of for over lines

    return matrix[nblines][nbcols];
  }

}

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Testing
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
TEST_CASE("Univariate ERP Fixed length", "[erp][univariate]") {
  // Setup univariate with fixed length
  Mocker mocker;
  const auto& wratios = mocker.wratios;
  const auto& gvalues = mocker.gvalues;

  const auto fset = mocker.vec_randvec(nbitems);

  SECTION("DTW(s,s) == 0") {
    for (const auto& s: fset) {
      for (double wr: wratios) {
        auto w = (size_t) (wr*mocker._fixl);
        for (auto gv: gvalues) {
          const double dtw_ref_v = reference::erp_matrix(s, s, gv, w);
          REQUIRE(dtw_ref_v==0);

          const auto dtw_v = erp<double>(s, s, gv, w);
          REQUIRE(dtw_v==0);
        }
      }
    }
  }

  SECTION("DTW(s1, s2)") {
    for (size_t i = 0; i<nbitems-1; ++i) {
      const auto& s1 = fset[i];
      const auto& s2 = fset[i+1];

      for (double wr: wratios) {
        const auto w = (size_t) (wr*mocker._fixl);

        for (auto gv: gvalues) {

          const double dtw_ref_v = reference::erp_matrix(s1, s2, gv, w);
          INFO("Exact same operation order. Expect exact floating point equality.")

          const auto dtw_tempo = erp<double>(s1, s2, gv, w);
          REQUIRE(dtw_ref_v==dtw_tempo);
        }
      }
    }
  }

  SECTION("NN1 DTW") {
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
      size_t idx_eap = 0;
      double bsf_eap = lu::PINF<double>;

      // NN1 loop
      for (size_t j = 0; j<nbitems; j += 5) {
        // Skip self.
        if (i==j) { continue; }
        const auto& s2 = fset[j];
        // Create the univariate squared Euclidean distance for our dtw functions
        for (double wr: wratios) {
          const auto w = (size_t) (wr*mocker._fixl);

          for (auto gv: gvalues) {
            // --- --- --- --- --- --- --- --- --- --- --- ---
            const double v_ref = reference::erp_matrix(s1, s2, gv, w);
            if (v_ref<bsf_ref) {
              idx_ref = j;
              bsf_ref = v_ref;
            }

            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v = erp<double>(s1, s2, gv, w);
            if (v<bsf) {
              idx = j;
              bsf = v;
            }

            REQUIRE(idx_ref==idx);

            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v_eap = erp<double>(s1, s2, gv, w, bsf_eap);
            if (v_eap<bsf_eap) {
              idx_eap = j;
              bsf_eap = v_eap;
            }

            REQUIRE(idx_ref==idx_eap);
          }
        }
      }
    }// End query loop
  }// End section

}

TEST_CASE("Univariate ERP Variable length", "[erp][univariate]") {
  // Setup univariate dataset with varying length
  Mocker mocker;
  const auto& wratios = mocker.wratios;
  const auto& gvalues = mocker.gvalues;

  const auto fset = mocker.vec_rs_randvec(nbitems);

  SECTION("DTW(s,s) == 0") {
    for (const auto& s: fset) {
      for (double wr: wratios) {
        const auto w = (size_t) (wr*(s.size()));
        for (auto gv: gvalues) {
          const double dtw_ref_v = reference::erp_matrix(s, s, gv, w);
          REQUIRE(dtw_ref_v==0);

          const auto dtw_v = erp<double>(s, s, gv, w);
          REQUIRE(dtw_v==0);
        }
      }
    }
  }

  SECTION("DTW(s1, s2)") {
    for (size_t i = 0; i<nbitems-1; ++i) {
      for (double wr: wratios) {
        const auto& s1 = fset[i];
        const auto& s2 = fset[i+1];
        const auto w = (size_t) (wr*(min(s1.size(), s2.size())));
        for (auto gv: gvalues) {
          const double dtw_ref_v = reference::erp_matrix(s1, s2, gv, w);
          INFO("Exact same operation order. Expect exact floating point equality.")

          const auto dtw_eap_v = erp<double>(s1, s2, gv, w, libtempo::utils::QNAN<double>);
          REQUIRE(dtw_ref_v==dtw_eap_v);
        }
      }
    }
  }

  SECTION("NN1 DTW") {
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
      size_t idx_eap = 0;
      double bsf_eap = lu::PINF<double>;

      // NN1 loop
      for (size_t j = 0; j<nbitems; j += 5) {
        // Skip self.
        if (i==j) { continue; }
        const auto& s2 = fset[j];
        // Create the univariate squared Euclidean distance for our dtw functions

        for (double wr: wratios) {
          const auto w = (size_t) (wr*(min(s1.size(), s2.size())));

          for (auto gv: gvalues) {

            // --- --- --- --- --- --- --- --- --- --- --- ---
            const double v_ref = reference::erp_matrix(s1, s2, gv, w);
            if (v_ref<bsf_ref) {
              idx_ref = j;
              bsf_ref = v_ref;
            }

            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v = erp<double>(s1, s2, gv, w);
            if (v<bsf) {
              idx = j;
              bsf = v;
            }

            REQUIRE(idx_ref==idx);

            // --- --- --- --- --- --- --- --- --- --- --- ---
            const auto v_eap = erp<double>(s1, s2, gv, w, bsf_eap);
            if (v_eap<bsf_eap) {
              idx_eap = j;
              bsf_eap = v_eap;
            }

            REQUIRE(idx_ref==idx_eap);
          }
        }
      }
    }// End query loop
  }// End section
}