#pragma once
#include <string>

// ETCD 
const std::string ECTD_URL = "http://etcd:2379";
const std::string modelservice_key = "/services/searchservice";
const std::string filename = "/data/raw_data.csv";
// const std::string filename = "/data/data.csv";

constexpr float epsilon = 1e-6;
constexpr size_t total_data_num = 600000000;
constexpr size_t key_word_num = 1e6;
constexpr size_t adgroup_id_num = 5e6;