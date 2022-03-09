#pragma once

#include <libtempo/utils/utils.hpp>
#include <libtempo/distance/cost_function.hpp>
#include <libtempo/concepts.hpp>

namespace libtempo::distance {

  /// MSM specific cost function concept, computing the cost when taking a non diagonal step (warping the alignments)
  /// All required information (series and cost) must be captured
  template<typename Fun, typename R>
  concept CFunMSM = Float<R> && requires(Fun fun, size_t i, size_t j){
    { fun(i, j) }->std::same_as<R>;
  };

  namespace internal {

    /** Move Split Merge (MSM), with cut-off point for early abandoning and pruning.
     *  Double buffered implementation using O(n) space.
     *  Worst case scenario has a O(n²) time complexity (no pruning nor early abandoning, large window).
     *  A tight cutoff can allow a lot of pruning, speeding up the process considerably.
     *  Actual implementation assuming that some pre-conditions are fulfilled.
     * @tparam FloatType    The floating number type used to represent the series.
     * @tparam D            Type of underlying collection - given to dist
     * @tparam FDist        Distance computation function, must be a (const D&, size_t, constD&, size_t)->FloatType
     * @tparam FCost        MSM cost computation function - see msm_cost_uni and msms_cost_multi
     * @param lines         Data for the lines
     * @param nblines       Length of the line series. Must be 0 < nbcols <= nblines
     * @param cols          Data for the lines
     * @param dist          Distance function of type FDist
     * @param nbcols        Length of the column series. Must be 0 < nbcols <= nblines
     * @param msms_cost     MSM cost function
     * @param co            MSM cost parameter, transmitted to msm_cost
     * @param cutoff        Attempt to prune computation of alignments with cost > cutoff.
     *                      May lead to early abandoning.
     * @return MSM value or +INF if early abandoned, or , given w, no alignment is possible
     */
    template<Float F>
    [[nodiscard]] inline F msm(
      const size_t nblines,
      const size_t nbcols,
      CFunMSM<F> auto dist_lines,
      CFunMSM<F> auto dist_cols,
      CFun<F> auto dist,
      F cutoff,
      std::vector<F>& buffer_v
    ) {
      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // In debug mode, check preconditions
      assert(nblines!=0);
      assert(nbcols!=0);
      assert(nbcols<=nblines);
      // Adapt constants to the floating point type
      using namespace utils;
      constexpr auto PINF = utils::PINF<F>;

      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // Create a new tighter upper bounds (most commonly used in the code).
      // First, take the "next float" after "cutoff" to deal with numerical instability.
      // Then, subtract the cost of the last alignment.
      const F ub = initBlock {
        // The last alignment can only computed if we have nbcols >= 2
        if (nbcols>=2) {
          const auto i = nblines-1;
          const auto j = nbcols-1;
          const auto la = min(
            dist(i, j),             // Diag: Move
            dist_cols(i, j),        // Previous: Split/Merge
            dist_lines(i, j)        // Above: Split/Merge
          );
          return F(nextafter(cutoff, PINF-la));
        } else {
          return F(cutoff); // Force type to prevent auto-deduction failure
        }
      };

      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // Double buffer allocation, no initialisation required (border condition manage in the code).
      // Base indices for the 'c'urrent row and the 'p'revious row.
      // auto buffer = std::unique_ptr<F[]>(new F[nbcols*2]);
      buffer_v.assign((nbcols*2), 0);
      auto* buffer = buffer_v.data();
      size_t c{0}, p{nbcols};

      // Line & column counters
      size_t i{0}, j{0};

      // Cost accumulator. Also used as the "left neighbour".
      double cost{0};

      // EAP variables: track where to start the next line, and the position of the previous pruning point.
      // Must be init to 0: index 0 is the next starting index and also the "previous pruning point"
      size_t next_start{0}, prev_pp{0};

      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // Initialisation: compute the first line. Required as the main loop starts at line=1, not 0.
      {
        // First cell (0,0) is a special case. Early abandon if above the cut-off point.
        {
          cost = dist(0, 0); // Very first cell
          buffer[c+0] = cost;
          if (cost<=ub) { prev_pp = 1; } else { return PINF; }
        }
        // Rest of the line, a cell only depends on the previous cell. Stop when > ub, update prev_pp.
        for (j = 1; j<nbcols; ++j) {
          cost = cost+dist_cols(0, j); // Previous
          buffer[c+j] = cost;
          if (cost<=ub) { prev_pp = j+1; } else { break; }
        }
        // Next line.
        ++i;
      }

      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // Main loop
      for (; i<nblines; ++i) {
        // --- --- --- Swap and variables init
        std::swap(c, p);
        size_t curr_pp = next_start; // Next pruning point init at the start of the line
        j = next_start;
        // --- --- --- Stage 0: Special case for the first column. Can only look up (border on the left)
        {
          cost = buffer[p+j]+dist_lines(i, j);  // Above
          buffer[c+j] = cost;
          if (cost<=ub) { curr_pp = j+1; } else { ++next_start; }
          ++j;
        }
        // --- --- --- Stage 1: Up to the previous pruning point while advancing next_start: diag and top
        for (; j==next_start && j<prev_pp; ++j) {
          cost = std::min(
            buffer[p+j-1]+dist(i, j),            // Diag: Move
            buffer[p+j]+dist_lines(i, j)         // Above: Split/Merge
          );
          buffer[c+j] = cost;
          if (cost<=ub) { curr_pp = j+1; } else { ++next_start; }
        }
        // --- --- --- Stage 2: Up to the previous pruning point without advancing next_start: left, diag and top
        for (; j<prev_pp; ++j) {
          cost = min(
            buffer[p+j-1]+dist(i, j),            // Diag: Move
            cost+dist_cols(i, j),                 // Previous: Split/Merge
            buffer[p+j]+dist_lines(i, j)         // Above: Split/Merge
          );
          buffer[c+j] = cost;
          if (cost<=ub) { curr_pp = j+1; }
        }
        // --- --- --- Stage 3: At the previous pruning point. Check if we are within bounds.
        if (j<nbcols) { // If so, two cases.
          if (j==next_start) { // Case 1: Advancing next start: only diag.
            cost = buffer[p+j-1]+dist(i, j);    // Diag: Move
            buffer[c+j] = cost;
            if (cost<=ub) { curr_pp = j+1; }
            else {
              // Special case if we are on the last alignment: return the actual cost if we are <= cutoff
              if (i==nblines-1 && j==nbcols-1 && cost<=cutoff) { return cost; }
              else { return PINF; }
            }
          } else { // Case 2: Not advancing next start: possible path in previous cells: left and diag.
            cost = std::min(
              buffer[p+j-1]+dist(i, j),        // Diag: Move
              cost+dist_cols(i, j)              // Previous: Split/Merge
            );
            buffer[c+j] = cost;
            if (cost<=ub) { curr_pp = j+1; }
          }
          ++j;
        } else { // Previous pruning point is out of bound: exit if we extended next start up to here.
          if (j==next_start) {
            // But only if we are above the original UB
            // Else set the next starting point to the last valid column
            if (cost>cutoff) { return PINF; }
            else { next_start = nbcols-1; }
          }
        }
        // --- --- --- Stage 4: After the previous pruning point: only prev.
        // Go on while we advance the curr_pp; if it did not advance, the rest of the line is guaranteed to be > ub.
        for (; j==curr_pp && j<nbcols; ++j) {
          cost = cost+dist_cols(i, j);    // Previous: Split/Merge
          buffer[c+j] = cost;
          if (cost<=ub) { ++curr_pp; }
        }
        // --- --- ---
        prev_pp = curr_pp;
      } // End of main loop for(;i<nblines;++i)

      // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
      // Finalisation
      // Check for last alignment (i==nblines implied, Stage 4 implies j<=nbcols). Cost must be <= original bound.
      if (j==nbcols && cost<=cutoff) { return cost; } else { return PINF; }
    }

  } // End of namespace internal



  /** Edit Distance with Real Penalty (MSM), with cut-off point for early abandoning and pruning.
   *  Double buffered implementation using O(n) space.
   *  Worst case scenario has a O(n²) time complexity (no pruning nor early abandoning, large window).
   *  A tight cutoff can allow a lot of pruning, speeding up the process considerably.
   *  Actual implementation assuming that some pre-conditions are fulfilled.
   * @tparam FloatType  The floating number type used to represent the series.
   * @tparam D          Type of underlying collection - given to dist
   * @tparam FDist      Distance computation function, must be a (const D&, size_t, constD&, size_t)->FloatType
   * @param series1     Data for the first series
   * @param length1     Length of the first series.
   * @param series2     Data for the second series
   * @param length2     Length of the second series.
   * @param dist        Distance function of type FDist
   * @param gv          Data for the gValue - must have the correct dimension!
   * @param w           Half-window parameter (looking at w cells on each side of the diagonal)
   *                    Must be 0<=w<=nblines and nblines - nbcols <= w
   * @param ub          Upper bound. Attempt to prune computation of alignments with cost > cutoff.
   *                    May lead to early abandoning.
   *                    ub = PINF: use pruning
   *                    ub = QNAN: no lower bounding
   *                    ub = other value: use for pruning and early abandoning
   * @return MSM value or +INF if early abandoned, or , given w, no alignment is possible
   */
  template<Float F>
  [[nodiscard]] F msm(
    const size_t nblines,
    const size_t nbcols,
    CFunMSM<F> auto dist_lines,
    CFunMSM<F> auto dist_cols,
    CFun<F> auto dist,
    F ub,
    std::vector<F>& buffer_v
  ) {
    constexpr F INF = utils::PINF<F>;
    if (nblines==0 && nbcols==0) { return 0; }
    else if ((nblines==0)!=(nbcols==0)) { return INF; }
    else {
      // Compute a cutoff point using the diagonal
      if (std::isinf(ub)) {
        const auto m = std::min(nblines, nbcols);
        ub = 0;
        // Cover diagonal
        for (size_t i{0}; i<m; ++i) { ub = ub+dist(i, i); }
        // Fewer line than columns: complete the last line (advancing in the columns)
        if (nblines<nbcols) {
          for (size_t j{nblines}; j<nbcols; ++j) { ub = ub+dist_cols(nblines-1, j); }
        }
          // Fewer columns than lines: complete the last column (advancing in the lines)
        else if (nbcols<nblines) {
          for (size_t i{nbcols}; i<nblines; ++i) { ub = ub+dist_lines(i, nbcols-1); }
        }
      } else if (std::isnan(ub)) { ub = INF; }
      // ub computed
      return internal::msm<F>(nblines, nbcols, dist_lines, dist_cols, dist, ub, buffer_v);
    }
  }

  /// Helper without having to provide a buffer
  template<Float F>
  [[nodiscard]] inline F msm(
    const size_t nblines,
    const size_t nbcols,
    CFunMSM<F> auto dist_lines,
    CFunMSM<F> auto dist_cols,
    CFun<F> auto dist,
    F ub) {
    std::vector<F> v;
    return msm<F>(nblines, nbcols, dist_lines, dist_cols, dist, ub, v);
  }

  /// CFunMSMBuilder: Function creating a CFunMSM based on 2 series and a cost c
  template<typename T, typename D, typename F>
  concept CFunMSMBuilder = Float<F> && requires(T builder, const D& s1, const D& s2, const F c){
    builder(s1, s2, c);
  };

  /// Helper for TSLike, without having to provide a buffer
  template<Float F, TSLike T>
  [[nodiscard]] inline F
  msm(const T& lines, const T& cols, const F c,
    CFunMSMBuilder<T, F> auto mkdist_lines,
    CFunMSMBuilder<T, F> auto mkdist_cols,
    CFunBuilder<T> auto mkdist,
    F ub = utils::PINF<F>) {
    const auto ls = lines.length();
    const auto cs = cols.length();
    const CFunMSM<F> auto dist_lines = mkdist_lines(lines, cols, c);
    const CFunMSM<F> auto dists_cols = mkdist_cols(lines, cols, c);
    const CFun<F> auto dist = mkdist(lines, cols);
    return msm<F>(ls, cs, dist_lines, dists_cols, dist, ub);
  }

  namespace univariate {

    /** Univariate cost function used when transforming X=(x1, x2, ... xi) into Y = (y1, ..., yj) by Split or Merge (symmetric)
     * @tparam FT    The floating number type used to represent the series.
     * @tparam D            Type of underlying collection - given to dist
     * @param X             Main series: the series where a new point is added (can be line or column!)
     * @param xnew_         In either X or Y
     * @param xi_           Last point of X
     * @param Y             The other series
     * @param yj_           Last point of Y
     * @param c             cost of split and merge operation
     * @return MSM cost of the xi-yj alignment (without "recursive" part)
     */
    template<Float F, Subscriptable D>
    [[nodiscard]] inline F _msm_cost_ad1(const D& X, size_t xnew_, size_t xi_, const D& Y, size_t yj_, F cost) {
      F xnew = X[xnew_];
      F xi = X[xi_];
      F yj = Y[yj_];
      if (((xi<=xnew) && (xnew<=yj)) || ((yj<=xnew) && (xnew<=xi))) { return cost; }
      else { return cost+std::min(std::abs(xnew-xi), std::abs(xnew-yj)); }
    }

    template<Float F, Subscriptable D>
    [[nodiscard]] inline auto msm_lines_ad1(const D& lines, const D& cols, const F c) {
      return [&lines, &cols, c](size_t i, size_t j) {
        return _msm_cost_ad1(lines, i, i-1, cols, j, c);
      };
    }

    template<Float F, Subscriptable D>
    [[nodiscard]] inline auto msm_cols_ad1(const D& lines, const D& cols, const F c) {
      return [&lines, &cols, c](size_t i, size_t j) {
        return _msm_cost_ad1(cols, j, j-1, lines, i, c);
      };
    }

    /// Default MSM using univariate ad1
    template<Float F, TSLike T>
    [[nodiscard]] inline F msm(const T& lines, const T& cols, const F c, F ub = utils::PINF<F>) {
      return libtempo::distance::msm(lines, cols, c, msm_lines_ad1, msm_cols_ad1, ad1, ub);
    }

    /// Specific overload for univariate vector
    template<Float F>
    [[nodiscard]] inline F msm(const std::vector<F>& lines, const std::vector<F>& cols, const F c,
      CFunMSMBuilder<std::vector<F>, F> auto mkdist_lines,
      CFunMSMBuilder<std::vector<F>, F> auto mkdist_cols,
      CFunBuilder<std::vector<F>> auto mkdist,
      F ub = utils::PINF<F>) {
      const auto ls = lines.size();
      const auto cs = cols.size();
      const CFunMSM<F> auto dist_lines = mkdist_lines(lines, cols, c);
      const CFunMSM<F> auto dists_cols = mkdist_cols(lines, cols, c);
      const CFun<F> auto dist = mkdist(lines, cols);
      return libtempo::distance::msm<F>(ls, cs, dist_lines, dists_cols, dist, ub);
    }

    /// Specific overload for univariate vector using ad1
    template<Float F>
    [[nodiscard]] inline F
    msm(const std::vector<F>& lines, const std::vector<F>& cols, const F c, F ub = utils::PINF<F>) {
      return msm(lines, cols, c,
        msm_lines_ad1<F, std::vector<F>>,
        msm_cols_ad1<F, std::vector<F>>,
        ad1<F, std::vector<F>>,
        ub);
    }

  }








  // /** Multivariate cost function when transforming
  //  *  X=(x1, x2, ... xi) into Y = (y1, ..., yj) by Split or Merge (symmetric - dependening how we move in the matrix).
  //  * @tparam FloatType    The floating number type used to represent the series.
  //  * @tparam D            Type of underlying collection - given to dist
  //  * @tparam FDist        Distance function, must be a (const D&, size_t, constD&, size_t)->FloatType
  //  * @tparam FDistMP      Distance to midpoint function, must be a (const D&, size_t, size_t, constD&, size_t)->FloatType
  //  * @param  X            Main series: the series where a new point is added (can be line or column!)
  //  * @param  xnew         Index of the new point in X
  //  * @param  Y            The other series
  //  * @param  xi           Index of the last point in X
  //  * @param  yi           Index of the last point in Y
  //  * From Shifaz et al 2021
  //  * Elastic Similarity Measures for Multivariate Time Series Classification
  //  * https://arxiv.org/abs/2102.10231
  //  *   We check if xnew is "between" xi an yi by checking if it is inside the hyper-sphere
  //  *   defined by xi and yi: take the midpoint between xi and yi and the sphere radius.
  //  *   If the distance between the midpoint and xnew > radius, xnew is not in the sphere.
  //  */
  // template<typename FloatType, typename D, typename FDist, typename FDistMP>
  // [[nodiscard]] inline FloatType msm_cost_multi(
  //   const D& X, size_t xnew, size_t xi,
  //   const D& Y, size_t yi,
  //   FDist dist,
  //   FDistMP distmpoint,
  //   FloatType cost
  // ) {
  //   const FloatType radius = dist(X, xi, Y, yi)/2; // distance between xi and yi give us the sphere diameter
  //   const FloatType d_to_mid = distmpoint(X, xnew, xi, Y, yi);
  //   if (d_to_mid<=radius) { return cost; }
  //   else {
  //     const FloatType d_to_prev = dist(X, xnew, X, xi);
  //     const FloatType d_to_other = dist(X, xnew, Y, yi);
  //     return cost+std::min<FloatType>(d_to_prev, d_to_other);
  //   }
  // }
  //


} // End of namespace libtempo::distance