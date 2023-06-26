#pragma once
#include <fstream>
#include <iostream>

#include "defines.h"
#include "json.hpp"

using json = nlohmann::json;

struct TestResultConfig {
  std::string test_case_csv;
  int32_t csv_reader_capacity;
  int32_t thread_num;
  int32_t timeout_ms;
  int32_t sample_num;
  int32_t qps_limit;
  int32_t final_score_th;
}; 

struct TestMaxQpsConfig {
  std::string test_case_csv;
  int32_t csv_reader_capacity;
  int32_t qps_upper;
  int32_t qps_baseline;
  int32_t qps_step0;
  int32_t qps_step1;
  int32_t thread_num;
  int32_t timeout_ms;
  int32_t request_duration_each_iter_sec;
  double sample_percent_th;
  double success_percent_th;
  int32_t sample_score_th;
}; 

struct TestCapacityConfig {
  std::string test_case_csv;
  int32_t csv_reader_capacity;
  int32_t thread_num;
  int32_t timeout_ms;
  int32_t sample_num;
  int32_t test_duration_sec;
  double  success_percent_th;
  double  sample_percent_th;
  int32_t sample_score_th;
};

struct TestResponseTimeConfig {
  std::string test_case_csv;
  int32_t csv_reader_capacity;
  int32_t thread_num;
  int32_t timeout_ms;
  int32_t test_duration_sec;
  int32_t qps_limit;
  double  success_percent_th;
  double  sample_percent_th;
  int32_t sample_score_th;
};

struct TestStabilityConfig {
  std::string test_case_csv;
  int32_t csv_reader_capacity;
  int32_t thread_num;
  int32_t max_qps;
  double load_percent;
  int32_t timeout_ms;
  int32_t test_duration_sec;
  double  success_percent_th;
  double  sample_percent_th;
  int32_t sample_score_th;
};


struct TestConfig {
  int32_t loading_time_sec_timeout;
  double  accuracy_th;
  int32_t M;
  TestResultConfig result_cfg;
  TestMaxQpsConfig max_qps_cfg;
  TestCapacityConfig capacity_cfg;
  TestResponseTimeConfig response_time_cfg;
  TestStabilityConfig stability_cfg;
};

void FromJson(const json& j, TestResultConfig& p) {
    p.thread_num = j.at("thread_num").get<int32_t>();
    p.timeout_ms = j.at("timeout_ms").get<int32_t>();
    p.qps_limit = j.at("qps_limit").get<int32_t>();
    p.sample_num = j.at("sample_num").get<int32_t>();
    p.test_case_csv = j.at("test_case_csv").get<std::string>();
    p.csv_reader_capacity = j.at("csv_reader_capacity").get<int32_t>();
    p.final_score_th = j.at("final_score_th").get<int32_t>();
}

void FromJson(const json& j, TestCapacityConfig& p) {
    p.test_case_csv = j.at("test_case_csv").get<std::string>();
    p.csv_reader_capacity = j.at("csv_reader_capacity").get<int32_t>();
    p.thread_num = j.at("thread_num").get<int32_t>();
    p.timeout_ms = j.at("timeout_ms").get<int32_t>();
    p.sample_num = j.at("sample_num").get<int32_t>();
    p.test_duration_sec = j.at("test_duration_sec").get<int32_t>();
    p.sample_percent_th = j.at("sample_percent_th").get<double>();
    p.success_percent_th = j.at("success_percent_th").get<double>();
    p.sample_score_th = j.at("sample_score_th").get<int32_t>();
}

void FromJson(const json& j, TestMaxQpsConfig& p) {
    p.thread_num = j.at("thread_num").get<int32_t>();
    p.timeout_ms = j.at("timeout_ms").get<int32_t>();
    p.qps_upper = j.at("qps_upper").get<int32_t>();
    p.qps_baseline = j.at("qps_baseline").get<int32_t>();
    p.qps_step0 = j.at("qps_step0").get<int32_t>();
    p.qps_step1 = j.at("qps_step1").get<int32_t>();
    p.request_duration_each_iter_sec = j.at("request_duration_each_iter_sec").get<int32_t>();
    p.sample_percent_th = j.at("sample_percent_th").get<double>();
    p.success_percent_th = j.at("success_percent_th").get<double>();
    p.sample_score_th = j.at("sample_score_th").get<int32_t>();
    p.test_case_csv = j.at("test_case_csv").get<std::string>();
    p.csv_reader_capacity = j.at("csv_reader_capacity").get<int32_t>();
}

void FromJson(const json& j, TestResponseTimeConfig& p) {
    p.thread_num = j.at("thread_num").get<int32_t>();
    p.timeout_ms = j.at("timeout_ms").get<int32_t>();
    p.test_duration_sec = j.at("test_duration_sec").get<int32_t>();
    p.test_case_csv = j.at("test_case_csv").get<std::string>();
    p.csv_reader_capacity = j.at("csv_reader_capacity").get<int32_t>();
    p.sample_percent_th = j.at("sample_percent_th").get<double>();
    p.success_percent_th = j.at("success_percent_th").get<double>();
    p.sample_score_th = j.at("sample_score_th").get<int32_t>();
    p.qps_limit = j.at("qps_limit").get<int32_t>();
}

void FromJson(const json& j, TestStabilityConfig& p) {
    p.thread_num = j.at("thread_num").get<int32_t>();
    p.load_percent = j.at("load_percent").get<double>();
    p.timeout_ms = j.at("timeout_ms").get<int32_t>();
    p.test_duration_sec = j.at("test_duration_sec").get<int32_t>();
    p.test_case_csv = j.at("test_case_csv").get<std::string>();
    p.csv_reader_capacity = j.at("csv_reader_capacity").get<int32_t>();
    p.sample_percent_th = j.at("sample_percent_th").get<double>();
    p.success_percent_th = j.at("success_percent_th").get<double>();
    p.sample_score_th = j.at("sample_score_th").get<int32_t>();
}

bool ReadConfigFromFile(const std::string& filename, TestConfig& config) {
    std::ifstream i(filename);
    if(!i.is_open()){
        std::cerr << "Could not open file: " << filename << std::endl;
        return false;
    }
    json j;
    try{
        i >> j;
    }
    catch (json::parse_error& e){
        std::cerr << "JSON parse error: " << e.what() << '\n';
        return false;
    }
    
    if(j.contains("test_result") && j["test_result"].is_object()) {
        FromJson(j["test_result"], config.result_cfg);
    }
    else {
        std::cerr << "Missing or invalid 'test_result' in JSON.\n";
        return false;
    }

    if(j.contains("test_max_qps") && j["test_max_qps"].is_object()) {
        FromJson(j["test_max_qps"], config.max_qps_cfg);
    } else {
        std::cerr << "Missing or invalid 'test_max_qps' in JSON.\n";
        return false;
    }

    if(j.contains("test_stability") && j["test_stability"].is_object()) {
        FromJson(j["test_stability"], config.stability_cfg);
    } else {
        std::cerr << "Missing or invalid 'test_stability' in JSON.\n";
        return false;
    }

    if(j.contains("test_response_time") && j["test_response_time"].is_object()) {
        FromJson(j["test_response_time"], config.response_time_cfg);
    } else {
        std::cerr << "Missing or invalid 'test_response_time' in JSON.\n";
        return false;
    }

    if(j.contains("test_capacity") && j["test_capacity"].is_object()) {
        FromJson(j["test_capacity"], config.capacity_cfg);
    } else {
        std::cerr << "Missing or invalid 'test_capacity' in JSON.\n";
        return false;
    }

    config.loading_time_sec_timeout = j.at("loading_time_sec_timeout").get<int32_t>();
    config.accuracy_th = j.at("accuracy_th").get<double>();
    config.M = j.at("M").get<int32_t>();
    return true;
}
