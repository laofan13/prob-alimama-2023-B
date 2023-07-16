#include <string>

// ETCD 
const std::string ECTD_URL = "http://etcd:2379";
const std::string modelservice_key = "/services/searchservice";
const std::string modelservice_node1 = "/services/node1";
const std::string filename = "/data/raw_data.csv";
// const std::string filename = "/data/data.csv";

// 568313896
// |282686718|285627178
// 190054042|189172001|189087853
// constexpr size_t total_data_num = 300000000;
constexpr size_t total_data_num = 10000000;
constexpr size_t key_word_num = 1e6;

// 1000 0000 1s
// 10000 0000