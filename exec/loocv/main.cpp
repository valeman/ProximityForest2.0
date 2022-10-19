#include "iloocv_adtw.cpp"
#include "iloocv_dtw.cpp"

#include <tempo/utils/utils.hpp>
#include <tempo/utils/readingtools.hpp>
#include <tempo/dataset/dts.hpp>
#include <tempo/reader/dts.reader.hpp>
#include <tempo/transform/tseries.univariate.hpp>
#include <tempo/classifier/loocv/partable/partable.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

[[noreturn]] void do_exit(int code, std::optional<std::string> msg = {}) {
  if (msg) { std::cerr << msg.value() << std::endl; }
  exit(code);
}

int main(int argc, char **argv) {

  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // CMDLine
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

  std::string program_name(*argv);
  std::vector<std::string> args(argv + 1, argv + argc);
  if (argc<7) {
    std::cout << "<path to ucr> <dataset name> <transform> <distance:cfe> <nbthreads> <output> required" << std::endl;
    exit(1);
  }
  std::vector<std::string> argList(argv, argv + argc);
  std::filesystem::path path_ucr(argList[1]);
  std::string dataset_name(argList[2]);
  std::string transform(argList[3]);
  std::string dist_cfe(argList[4]);
  size_t nbthreads = stoi(argList[5]);
  std::filesystem::path outpath(argList[6]);

  // Check distance
  std::vector<std::string> vdist_ge = tempo::reader::split(dist_cfe, ':');
  std::string distance_name = vdist_ge[0];
  double cfe = 1;
  if (vdist_ge.size()>1) {
    auto mb_cfe = tempo::reader::as_double(vdist_ge[1]);
    if (mb_cfe) {
      cfe = mb_cfe.value();
    } else {
      std::cout << "<distance:cfe>  <cfe> must be a double" << std::endl;
      exit(1);
    }
  }

  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // Prepare result
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  nlohmann::json jv;
  std::ofstream outfile(outpath);


  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // Read dataset
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  tempo::DTS raw_train;
  tempo::DTS raw_test;
  {
    using namespace tempo::reader::dataset;
    Result read_dataset_result;
    ts_ucr conf;
    conf.ucr_dir = path_ucr;
    conf.name = dataset_name;

    read_dataset_result = load({conf});

    if (read_dataset_result.index()==0) { do_exit(1, std::get<0>(read_dataset_result)); }
    TrainTest traintest = std::get<1>(std::move(read_dataset_result));
    raw_train = traintest.train_dataset;
    raw_test = traintest.test_dataset;

    nlohmann::json dataset;
    dataset["train"] = raw_train.header().to_json();
    dataset["test"] = raw_test.header().to_json();
    dataset["load_time_ns"] = traintest.load_time.count();
    dataset["load_time_str"] = tempo::utils::as_string(traintest.load_time);
    jv["dataset"] = dataset;

    // --- --- --- Sanity check
    std::vector<std::string> errors = sanity_check(traintest);

    if (!errors.empty()) {
      jv["status"] = "error";
      jv["status_message"] = tempo::utils::cat(errors, "; ");
      std::cout << to_string(jv) << std::endl;
      outfile << jv << std::endl;
      exit(0);
    }
  } // End of dataset loading

  const size_t train_size = raw_train.header().size();
  const size_t test_size = raw_test.header().size();

  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // Check the transform
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  tempo::DTS train = raw_train;
  tempo::DTS test = raw_test;
  {
    auto derive = [](tempo::TSeries const& ts) { return tempo::transform::univariate::derive(ts); };

    if (transform=="derivative") {
      {
        auto train_d = raw_train.transform().map_shptr<tempo::TSeries>(derive, transform);
        train = tempo::DTS("train", train_d);
      }
      {
        auto test_d = raw_test.transform().map_shptr<tempo::TSeries>(derive, transform);
        test = tempo::DTS("test", test_d);
      }
    } else if (transform=="raw") {
      //
    } else {
      do_exit(1, "Wrong transform name");
    }
  }

  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // Prepare the distances/argument range (EE style)
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  std::random_device rd;
  tempo::PRNG prng(rd());
  std::shared_ptr<tempo::classifier::nn1loocv::i_LOOCVDist> iloocv;
  std::function<nlohmann::json(void)> distance_json;

  if (distance_name=="ADTW") {
    auto adtw = std::make_shared<ADTW>(train, test, cfe, prng);
    iloocv = adtw;
    distance_json = [adtw]() { return adtw->to_json(); };
  } else if (distance_name=="DTW") {
    auto dtw = std::make_shared<DTW>(train, test, cfe);
    iloocv = dtw;
    distance_json = [dtw]() { return dtw->to_json(); };
  } else {
    do_exit(2, "Unknown distance " + distance_name);
  }


  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // Run LOOCV and test
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  tempo::classifier::nn1loocv::partable(
    *iloocv, train_size, train.header(), test_size, test.header(), prng, nbthreads, &std::cout
  );

  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  // --- Output
  // --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
  jv["status"] = "success";
  jv["distance"] = distance_json();
  jv["loocv_train"] = iloocv->result_train.to_json();
  jv["loocv_test"] = iloocv->result_test.to_json();

  std::cout << dataset_name << " output to " << outpath << std::endl;
  std::cout << jv.dump(2) << std::endl;
  outfile << jv << std::endl;

  std::cout << std::endl;

  std::cout << dataset_name << " LOOCV result: "
            << iloocv->result_train.nb_correct << "/" << train_size << " = " << iloocv->result_train.accuracy
            << "  (" << tempo::utils::as_string(iloocv->result_train.time) << ")" << std::endl;

  std::cout << dataset_name << " NN1 test result: "
            << iloocv->result_test.nb_correct << "/" << test_size << " = " << iloocv->result_test.accuracy
            << "  (" << tempo::utils::as_string(iloocv->result_test.time) << ")" << std::endl;

  return 0;
}
