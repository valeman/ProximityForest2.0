
#include "cli.hpp"

std::string usage =
  "Time Series NNK Classification - demonstration application\n"
  "Monash University, Melbourne, Australia 2022\n"
  "Dr. Matthieu Herrmann\n"
  "This application works with the UCR archive using the TS file format (or any archive following the same conventions).\n"
  "Only for univariate series.\n"
  "nnk <-p:> <-d:> [-n:] [-t:] [-k:] [-et:] [-seed:] [-out:]\n"
  "Mandatory arguments:\n"
  "  -p:<UCR path>:<dataset name>             e.g. '-p:/home/myuser/Univariate_ts:Adiac'\n"
  "  -d:<distance>\n"
  "    Lockstep:\n"
  "    -d:modminkowski:<float e>              Modified Minkowski distance with exponent 'e'\n"
  "                                           Does not take the e-th root of the result.\n"
  "    -d:lorentzian                          Lorentzian distance\n"
  "\n"
  "    Sliding:\n"
  "    -d:sbd                                 Shape Base Distance\n"
  "\n"
  "    *** Elastic:\n"
  "    -d:dtw:<float e>:<int w>               DTW with cost function exponent 'e' and warping window 'w'.\n"
  "                                           'w'<0 means no window\n"
  "    -d:adtw:<float e>:<float omega>        ADTW with cost function exponent 'e' and penalty 'omega'\n"
  "    -d:wdtw:<flaot e>:<float g>            WDTW with cost function exponent 'e' and weight factor 'g'\n"
  "    -d:erp:<float e>:<float gv>:<int w>    ERP with cost function exponent 'e', gap value 'gv' and warping window 'w'\n"
  "                                           'w'<0 means no window\n"
  "    -d:lcss:<float epsilon>:<int w>        LCSS with margin 'epsilon' and warping window 'w'\n"
  "    -d:msm:<float c>                       MSM with cost 'c'\n"
  "Optional arguments [with their default values]:\n"
  "  Normalisation:  applied before the transformation\n"
  "  -n:<normalisation>\n"
  "    -n:meannorm                            Mean Norm normalisation of the series\n"
  "    -n:minmax:[<min 0:max 1>]              MinMax normalisation of the series; by default in <0:1>\n"
  "    -n:unitlength                          Unitlength normalisation of the series\n"
  "    -n:zscore                              ZScore normalisation of the series\n"
  "\n"
  "  Transformation:  applied after the transformation\n"
  "  -t:<transformation>\n"
  "    -t:derivative:<int degree>             Compute the 'degree'-th derivative of the series\n"
  "\n"
  "  Other:\n"
  "  -et:<int n>     Number of execution threads. Autodetect if n=<0 [n = 0]\n"
  "  -k:<int n>      Number of neighbours to search [n = 1])\n"
  "  -seed:<int n>   Fixed seed of randomness. Generate a random seed if n<0 [n = -1] !\n"
  "  -out:<path>     Where to write the json file. If the file exists, overwrite it."
  "";

[[noreturn]] void do_exit(int code, std::optional<std::string> msg) {
  if (code==0) {
    if (msg) { std::cout << msg.value() << std::endl; }
  } else {
    std::cerr << usage << std::endl;
    if (msg) { std::cerr << msg.value() << std::endl; }
  }
  exit(code);
}


// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Optional args
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

void cmd_optional(std::vector<std::string> const& args, Config& conf) {

  // Value for k
  {
    auto p_k = tempo::scli::get_parameter<long long>(args, "-k", tempo::scli::extract_int, 1);
    if (p_k<=0) { do_exit(1, "-k must be followed by a integer >= 1"); }
    conf.k = p_k;
  }

  // Number of threads
  {
    auto p_et = tempo::scli::get_parameter<long long>(args, "-et", tempo::scli::extract_int, 0);
    if (p_et<0) { do_exit(1, "-et must specify a number of threads > 0, or 0 for auto-detect"); }
    if (p_et==0) { conf.nbthreads = std::thread::hardware_concurrency() + 2; } else { conf.nbthreads = p_et; }
  }

  // Random
  {
    auto p_seed = tempo::scli::get_parameter<long long>(args, "-seed", tempo::scli::extract_int, -1);
    if (p_seed<0) { conf.seed = std::random_device()(); } else { conf.seed = p_seed; }
    conf.pprng = std::make_unique<tempo::PRNG>(conf.seed);
  }

  // Output file
  {
    auto p_out = tempo::scli::get_parameter<std::string>(args, "-out", tempo::scli::extract_string);
    if (p_out) { conf.outpath = {std::filesystem::path{p_out.value()}}; }
  }
}

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Normalisation
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

/// MeanNorm normalisation -n:meannorm
bool n_meannorm(std::vector<std::string> const& v, Config& conf) {
  using namespace tempo;
  using namespace std;
  if (v[0]=="meannorm") {
    bool ok = v.size()==1;
    if (ok) {
      // Extract params
      // none

      // Do the normalisation
      auto f = [](TSeries const& input) -> TSeries { return transform::meannorm(input); };

      auto train_ptr = std::make_shared<DatasetTransform<TSeries>>
      (conf.loaded_train_split.transform().map<TSeries>(f, "meannorm"));
      conf.loaded_train_split = DTS(conf.loaded_train_split, train_ptr);

      auto test_ptr = std::make_shared<DatasetTransform<TSeries>>
      (conf.loaded_test_split.transform().map<TSeries>(f, "meannorm"));
      conf.loaded_test_split = DTS(conf.loaded_test_split, test_ptr);

      // Record params
      // none

    }
    // Catchall
    if (!ok) { do_exit(1, "MeanNorm parameter error"); }
    return true;
  }
  return false;
}

/// MinMax normalisation -n:minmax
bool n_minmax(std::vector<std::string> const& v, Config& conf) {
  using namespace tempo;
  using namespace std;
  if (v[0]=="minmax") {
    bool ok = v.size()==1||v.size()==3;
    if (ok) {
      // Extract params
      double min_range = 0;
      double max_range = 1;
      if (v.size()==3) {
        auto omin = tempo::reader::as_double(v[1]);
        auto omax = tempo::reader::as_double(v[2]);
        ok = omin.has_value()&&omax.has_value();
        if (ok) {
          min_range = omin.value();
          max_range = omax.value();
        }
      }
      if (ok) {
        // Do the normalisation
        auto f = [=](TSeries const& input) -> TSeries { return transform::minmax(input, min_range, max_range); };

        auto train_ptr = std::make_shared<DatasetTransform<TSeries>>
        (conf.loaded_train_split.transform().map<TSeries>(f, "minmax"));
        conf.loaded_train_split = DTS(conf.loaded_train_split, train_ptr);

        auto test_ptr = std::make_shared<DatasetTransform<TSeries>>
        (conf.loaded_test_split.transform().map<TSeries>(f, "minmax"));
        conf.loaded_test_split = DTS(conf.loaded_test_split, test_ptr);

        // Record params
        conf.norm_min_range = {min_range};
        conf.norm_max_range = {max_range};
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "MinMax parameter error"); }
    return true;
  }
  return false;
}

/// Unit Length normalisation -n:unitlength
bool n_unitlength(std::vector<std::string> const& v, Config& conf) {
  using namespace tempo;
  using namespace std;
  if (v[0]=="unitlength") {
    bool ok = v.size()==1;
    if (ok) {
      // Extract params
      // none

      // Do the normalisation
      auto f = [](TSeries const& input) -> TSeries { return transform::unitlength(input); };

      auto train_ptr = std::make_shared<DatasetTransform<TSeries>>
      (conf.loaded_train_split.transform().map<TSeries>(f, "unitlength"));
      conf.loaded_train_split = DTS(conf.loaded_train_split, train_ptr);

      auto test_ptr = std::make_shared<DatasetTransform<TSeries>>
      (conf.loaded_test_split.transform().map<TSeries>(f, "unitlength"));
      conf.loaded_test_split = DTS(conf.loaded_test_split, test_ptr);

      // Record params
      // none

    }
    // Catchall
    if (!ok) { do_exit(1, "MeanNorm parameter error"); }
    return true;
  }
  return false;
}

/// Zscore normalisation -n:zscore
bool n_zscore(std::vector<std::string> const& v, Config& conf) {
  using namespace tempo;
  using namespace std;
  if (v[0]=="zscore") {
    bool ok = v.size()==1;
    if (ok) {

      auto train_ptr = std::make_shared<DatasetTransform<TSeries>>
      (conf.loaded_train_split.transform().map<TSeries>(static_cast<TSeries(*)(TSeries const&)>(transform::zscore),
                                                        "zscore"
      ));
      conf.loaded_train_split = DTS(conf.loaded_train_split, train_ptr);

      auto test_ptr = std::make_shared<DatasetTransform<TSeries>>
      (conf.loaded_test_split.transform().map<TSeries>(static_cast<TSeries(*)(TSeries const&)>(transform::zscore),
                                                       "zscore"
      ));
      conf.loaded_test_split = DTS(conf.loaded_test_split, test_ptr);

    }
    // Catchall
    if (!ok) { do_exit(1, "ZScore parameter error"); }
    return true;
  }
  return false;
}

/// Command line parsing: special helper for the configuration of the normalisation
void cmd_normalisation(std::vector<std::string> const& args, Config& conf) {
  using namespace std;
  using namespace tempo;

  // Optional -n flag
  auto parg_normalise = tempo::scli::get_parameter<string>(args, "-n", tempo::scli::extract_string);
  if (parg_normalise) {
    // Split on ':'
    std::vector<std::string> v = tempo::reader::split(parg_normalise.value(), ':');
    conf.normalisation_name = v[0];
    // --- --- --- --- --- ---
    // Try parsing distance argument
    if (n_meannorm(v, conf)) {}
    else if (n_minmax(v, conf)) {}
    else if (n_unitlength(v, conf)) {}
    else if (n_zscore(v, conf)) {}
    else if (conf.normalisation_name=="default") {}
      // --- --- --- --- --- ---
      // Unknown transform
    else { do_exit(1, "Unknown normalisation '" + v[0] + "'"); }
  } else {
    // Default normalisation
    conf.normalisation_name = "default";
  }

}


// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// Transform
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

/// Derivative -t:derivative:<degree>
bool t_derivative(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="derivative") {
    bool ok = v.size()==2;
    if (ok) {
      auto od = tempo::reader::as_int(v[1]);
      ok = od.has_value();
      if (ok) {

        int degree = od.value();
        conf.param_derivative_degree = {degree};

        auto train_derive_t = std::make_shared<DatasetTransform<TSeries>>(
          std::move(tempo::transform::derive(conf.loaded_train_split.transform(), degree).back())
        );
        conf.train_split = DTS("train", train_derive_t);

        auto test_derive_t = std::make_shared<DatasetTransform<TSeries>>(
          std::move(tempo::transform::derive(conf.loaded_test_split.transform(), degree).back())
        );

        conf.test_split = DTS("test", test_derive_t);
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "Derivative parameter error"); }
    return true;
  }
  return false;
}

/// Command line parsing: special helper for the configuration of the transform
void cmd_transform(std::vector<std::string> const& args, Config& conf) {
  using namespace std;
  using namespace tempo;

  // Optional -t flag
  auto parg_transform = tempo::scli::get_parameter<string>(args, "-t", tempo::scli::extract_string);
  if (parg_transform) {
    // Split on ':'
    std::vector<std::string> v = tempo::reader::split(parg_transform.value(), ':');
    conf.transform_name = v[0];
    // --- --- --- --- --- ---
    // Try parsing distance argument
    if (t_derivative(v, conf)) {}
    else if (conf.transform_name=="default") {
      conf.train_split = conf.loaded_train_split;
      conf.test_split = conf.loaded_test_split;
    }
      // --- --- --- --- --- ---
      // Unknown transform
    else { do_exit(1, "Unknown transform '" + v[0] + "'"); }
  } else {
    // Default transform
    conf.transform_name = "default";
    conf.train_split = conf.loaded_train_split;
    conf.test_split = conf.loaded_test_split;
  }

}


// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
// DISTANCE
// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

// --- --- --- Lockstep

/// Minkowski -d:minkowski:<e>
bool d_minkowski(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;
  if (v[0]=="modminkowski") {
    bool ok = v.size()==2;
    if (ok) {
      auto oe = tempo::reader::as_double(v[1]);
      ok = oe.has_value();
      if (ok) {
        // Extract params
        double param_cf_exponent = oe.value();
        // Create the distance
        conf.dist_fun = [=](TSeries const& A, TSeries const& B, double /* ub */) -> double {
          return distance::minkowski(A, B, param_cf_exponent);
        };
        // Record params
        conf.param_cf_exponent = {param_cf_exponent};
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "Minkowski parameter error"); }
    return true;
  }
  return false;
}

/// Lorentzian -d:lorentzian
bool d_lorentzian(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;
  if (v[0]=="lorentzian") {
    bool ok = v.size()==1;
    if (ok) {
      // Extract params
      // none
      // Create the distance
      conf.dist_fun = [=](TSeries const& A, TSeries const& B, double /* ub */) -> double {
        return distance::lorentzian(A, B);
      };
      // Record params
      // none
    }
    // Catchall
    if (!ok) { do_exit(1, "Lorentzian parameter error"); }
    return true;
  }
  return false;
}


// --- --- --- Sliding

/// SBD -d:sbd
bool d_sbd(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;
  if (v[0]=="sbd") {
    bool ok = v.size()==1;
    if (ok) {
      // Extract params
      // none
      // Create the distance
      conf.dist_fun = [=](TSeries const& A, TSeries const& B, double /* ub */) -> double {
        return distance::SBD(A, B);
      };
      // Record params
      // none
    }
    // Catchall
    if (!ok) { do_exit(1, "SBD parameter error"); }
    return true;
  }
  return false;
}

// --- --- --- Elastic

/// DTW -d:dtw:<e>:<w>
bool d_dtw(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="dtw") {
    bool ok = v.size()==3;
    if (ok) {
      auto oe = tempo::reader::as_double(v[1]);
      optional<long> ow = tempo::reader::as_int(v[2]);
      ok = oe.has_value()&&ow.has_value();
      if (ok) {
        // Extract params
        double param_cf_exponent = oe.value();
        size_t param_window = utils::NO_WINDOW;
        if (ow.value()>=0) { param_window = ow.value(); }
        // Create the distance
        conf.dist_fun = [=](TSeries const& A, TSeries const& B, double ub) -> double {
          return distance::dtw(
            A.size(),
            B.size(),
            distance::univariate::ade<TSeries>(param_cf_exponent)(A, B),
            param_window,
            ub
          );
        };
        // Record params
        conf.param_cf_exponent = {param_cf_exponent};
        conf.param_window = {-1};
        if (ow.value()>=0) { conf.param_window = ow.value(); }
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "DTW parameter error"); }
    return true;
  }

  return false;
}

/// ADTW -d:adtw:<e>:<omega>
bool d_adtw(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="adtw") {
    bool ok = v.size()==3;
    if (ok) {
      auto oe = tempo::reader::as_double(v[1]);
      auto oo = tempo::reader::as_double(v[2]);
      ok = oe.has_value()&&oo.has_value();
      if (ok) {
        // Extract params
        double param_cf_exponent = oe.value();
        double param_omega = oo.value();
        // Create the distance
        conf.dist_fun = [=](TSeries const& A, TSeries const& B, double ub) -> double {
          return distance::adtw(
            A.size(),
            B.size(),
            distance::univariate::ade<TSeries>(param_cf_exponent)(A, B),
            param_omega,
            ub
          );
        };
        // Record params
        conf.param_cf_exponent = {param_cf_exponent};
        conf.param_omega = param_omega;
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "ADTW parameter error"); }
    return true;
  }
  return false;
}

/// WDTW -d:wdtw:<e>:<g>
bool d_wdtw(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="wdtw") {
    bool ok = v.size()==3;
    if (ok) {
      auto oe = reader::as_double(v[1]);
      auto og = reader::as_double(v[2]);
      ok = oe.has_value()&&og.has_value();
      if (ok) {
        // Extract params
        double param_cf_exponent = oe.value();
        double param_g = og.value();
        // Create the distance
        const auto& header_train = conf.loaded_train_split.header();
        const auto& header_test = conf.loaded_test_split.header();
        size_t length = std::max(header_train.length_max(), header_test.length_max());
        std::vector<F> weights = distance::generate_weights(param_g, length, distance::WDTW_MAX_WEIGHT);
        //
        conf.dist_fun = [=, w = std::move(weights)](TSeries const& A, TSeries const& B, double ub) -> double {
          return distance::wdtw(
            A.size(),
            B.size(),
            distance::univariate::ade<TSeries>(param_cf_exponent)(A, B),
            w,
            ub
          );
        };
        // Record params
        conf.param_cf_exponent = param_cf_exponent;
        conf.param_g = param_g;
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "WDTW parameter error"); }
    return true;
  }
  return false;

}

/// ERP -d:erp:<e>:<gv>:<w>
bool d_erp(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="erp") {
    bool ok = v.size()==4;
    if (ok) {
      optional<double> oe = tempo::reader::as_double(v[1]);
      optional<double> ogv = tempo::reader::as_double(v[2]);
      optional<long> ow = tempo::reader::as_int(v[3]);
      ok = oe.has_value()&&ogv.has_value()&&ow.has_value();
      if (ok) {
        // Extract params
        double param_cf_exponent = oe.value();
        double param_gv = ogv.value();
        size_t param_window = utils::NO_WINDOW;
        if (ow.value()>=0) { param_window = ow.value(); }
        // Create the distance
        conf.dist_fun = [=](TSeries const& A, TSeries const& B, double ub) -> double {
          return distance::erp(
            A.size(),
            B.size(),
            tempo::distance::univariate::adegv<TSeries>(param_cf_exponent)(A, param_gv),
            tempo::distance::univariate::adegv<TSeries>(param_cf_exponent)(B, param_gv),
            distance::univariate::ade<TSeries>(param_cf_exponent)(A, B),
            param_window,
            ub
          );
        };
        // Record params
        conf.param_cf_exponent = {param_cf_exponent};
        conf.param_gap_value = {param_gv};
        conf.param_window = {-1};
        if (ow.value()>=0) { conf.param_window = ow.value(); }
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "ERP parameter error"); }
    return true;
  }
  return false;

}

/// LCSS -d:lcss:<epsilon>:<w>
bool d_lcss(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="lcss") {
    bool ok = v.size()==3;
    if (ok) {
      auto oe = reader::as_double(v[1]);
      auto ow = reader::as_long(v[2]);
      ok = oe.has_value()&&ow.has_value();
      if (ok) {
        // Extract params
        double param_epsilon = oe.value();
        size_t param_window = utils::NO_WINDOW;
        if (ow.value()>=0) { param_window = ow.value(); }
        // Create the distance
        conf.dist_fun = [=](TSeries const& A, TSeries const& B, double ub) -> double {
          return distance::lcss(
            A.size(),
            B.size(),
            distance::univariate::ad1<TSeries>(A, B),
            param_window,
            param_epsilon,
            ub
          );
        };
        // Record params
        conf.param_epsilon = param_epsilon;
        conf.param_window = param_window;
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "LCSS parameter error"); }
    return true;
  }
  return false;
}

/// MSM -d:msm:<c>
bool d_msm(std::vector<std::string> const& v, Config& conf) {
  using namespace std;
  using namespace tempo;

  if (v[0]=="msm") {
    bool ok = v.size()==2;
    if (ok) {
      auto oc = reader::as_double(v[1]);
      ok = oc.has_value();
      if (ok) {
        // Extract params
        double param_c = oc.value();
        // Create the distance
        conf.dist_fun = [=](TSeries const& A, TSeries const& B, double ub) -> double {
          return distance::univariate::msm<TSeries>(A, B, param_c, ub);
        };
        // Record params
        conf.param_c = param_c;
      }
    }
    // Catchall
    if (!ok) { do_exit(1, "MSM parameter error"); }
    return true;
  }
  return false;
}

/// TWE -d:twe:<e>:<lambda>:<nu>
bool d_twe(std::vector<std::string> const& v, Config& conf) {

}


// --- --- --- All distances

/// Command line parsing: special helper for the configuration of the distance
void cmd_dist(std::vector<std::string> const& args, Config& conf) {
  using namespace std;
  using namespace tempo;

  // We must find a '-d' flag, else error
  auto parg_dist = tempo::scli::get_parameter<string>(args, "-d", tempo::scli::extract_string);
  if (!parg_dist) { do_exit(1, "specify a distance to use with '-d'"); }

  // Split on ':'
  std::vector<std::string> v = tempo::reader::split(parg_dist.value(), ':');
  conf.dist_name = v[0];

  // --- --- --- --- --- ---
  // Try parsing distance argument
  // Lockstep
  if (d_minkowski(v, conf)) {}
  else if (d_lorentzian(v, conf)) {}
    // Sliding
  else if (d_sbd(v, conf)) {}
    // Elastic
  else if (d_dtw(v, conf)) {}
  else if (d_adtw(v, conf)) {}
  else if (d_wdtw(v, conf)) {}
  else if (d_erp(v, conf)) {}
  else if (d_lcss(v, conf)) {}
  else if (d_msm(v, conf)) {}
    // --- --- --- --- --- ---
    // Unknown distance
  else { do_exit(1, "Unknown distance '" + v[0] + "'"); }

}
