#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>

#include "defines.h"
#include "utils.h"

#include "test_case_reader.h"
#include "test_case_reader_async.h"
#include "test_search_service.h"
#include "test_case_reader_preload.h"

struct Statistic { //用于最后评分的统计信息
  double result_score = 0.0; // （结果正确性得分项）判分规则：ad集合全部正确加50分，相对顺序正确加30分，扣费（bid_prices）加20分； 备注：随机抽样判定500个请求即可以，验证期间超时时间设定为500ms；
  double max_qps;
  double capacity_score  = 0.0;  // （最大吞吐得分项）判分规则：假设最大吞吐为Xqps（最大超时时间100ms下超时率<0.1%），本项得分= 100*2X/3M，其中M为benchark的QPS数，超过1.5倍的M，则得分100；
  double p99_latency_ms;
  double response_time_score  = 0.0; // （响应时间得分项）判分规则：假设P99响应时间为T（ms），本项得分= （100-T)^2 / 100，超过100ms不得分;  备注：在上述最大吞吐条件下，同时此处的100ms根据主办方baseline实际给出
  double service_score  = 0.0; //（服务稳定性得分项）判分规则：以交付系统最大服务容量的60%压力（包括rainy case）压测5分钟，服务出现异常得0分，正常服务得100分；
  double final_score  = 0.0; // 评分逻辑：Score= 0.5 * ResultScore +  0.2 * ResponseTimeScore + 0.2 * CapacityScore + 0.1 * ServiceScore 
  double M = 1000; // M为benchark的QPS数
  double timeout_percent; // 当判题异常停止时，输出超时比例
};


bool TestResulCalcStat(std::vector<std::string> services, Statistic& stat) {
    BOOST_LOG_TRIVIAL(trace)  << "reader start ";
    auto& cfg = g_config.result_cfg;
    auto reader = TestCaseReaderPreload(cfg.test_case_csv, cfg.csv_reader_capacity);
    reader.Start();

    BOOST_LOG_TRIVIAL(info) << "TestResultScore ";
    auto summary = TestResultScore(services, reader, cfg);
    reader.Stop();

    auto& user_summary = summary.custom_summary;
    double avg_score = user_summary.total_score / user_summary.total_num;
    stat.result_score = avg_score;
    stat.timeout_percent = summary.timeout_request_count * 1.0 / summary.completed_requests;
    BOOST_LOG_TRIVIAL(info) << "The number of correct ads is: " << user_summary.correct_ads_num;
    BOOST_LOG_TRIVIAL(info) << "The number of ordered correct ads is: " << user_summary.ordered_correct_ads_num;
    BOOST_LOG_TRIVIAL(info) << "The number of accurate prices ads is: " << user_summary.accurate_prices_ads_num;
    BOOST_LOG_TRIVIAL(info) << "current average score:  " << avg_score;
    double kSuccessPercentTh {0.999f};
    if (IsLess(summary.success_request_percent, kSuccessPercentTh)) {
      stat.result_score = 0;
      BOOST_LOG_TRIVIAL(info) << "exit testbench: success_request_percent < " << kSuccessPercentTh;
      BOOST_LOG_TRIVIAL(info) << "result score:  " << stat.result_score;
      return false;
    }
    BOOST_LOG_TRIVIAL(info) << "result score:  " << stat.result_score;
    if (IsLess(avg_score, cfg.final_score_th)) {
      BOOST_LOG_TRIVIAL(info) << "exit testbench: final_score_th < " << cfg.final_score_th;
      return false;
    }
    return true;
}

bool TestMaxQpsCalcStat(std::vector<std::string> services, Statistic& stat) {
    auto& cfg = g_config.max_qps_cfg;
    auto reader = TestCaseReaderPreload(cfg.test_case_csv, cfg.csv_reader_capacity);
    reader.Start();

    BOOST_LOG_TRIVIAL(info) << "TestMaxQps ";
    auto summary = TestMaxQps(services, reader, cfg);
    auto& custom_summary = summary.custom_summary;

    stat.max_qps = custom_summary.qps;
    BOOST_LOG_TRIVIAL(info)  << "max_qps " << stat.max_qps;
    stat.timeout_percent = summary.timeout_request_count * 1.0 / summary.completed_requests;
    if (summary.interupted || IsLess(summary.success_request_percent, cfg.success_percent_th)) {
      stat.max_qps = 0;
      BOOST_LOG_TRIVIAL(info) << "capacity score " << stat.capacity_score;
      return false;
    }
    double avg_score = custom_summary.total_score / custom_summary.total_num;
    if (IsLess(avg_score, cfg.sample_score_th)) {
      return false;
    }

    stat.M = g_config.M;
    stat.capacity_score = (100 * 2 * stat.max_qps) / (3 * g_config.M);
    if (stat.capacity_score > 100) stat.capacity_score = 100;
    BOOST_LOG_TRIVIAL(info) << "capacity score" << stat.capacity_score;

    reader.Stop();
    return true;
}

bool TestResponseTimeCalcStat(std::vector<std::string> services, Statistic& stat) {
    auto& cfg = g_config.response_time_cfg;
    auto reader = TestCaseReaderPreload(cfg.test_case_csv, cfg.csv_reader_capacity);
    reader.Start();

    BOOST_LOG_TRIVIAL(info) << "TestResponseTime ";
    auto summary = TestResponseTime(services, reader, cfg);
    reader.Stop();

    stat.timeout_percent = summary.timeout_request_count * 1.0 / summary.completed_requests;
    stat.response_time_score = 0;
    auto& custom_summary = summary.custom_summary;
    double avg_score = custom_summary.total_score / custom_summary.total_num;
    if (IsLess(avg_score, cfg.sample_score_th)) {
      return false;
    }

    auto qps = custom_summary.qps;
    if (summary.interupted || IsLess(summary.success_request_percent, cfg.success_percent_th)) {
      BOOST_LOG_TRIVIAL(info)  << "qps " << qps << " response_time_score " << stat.response_time_score;
      return false;
    }
    stat.p99_latency_ms = summary.p99_latency_ms;
    stat.response_time_score = stat.p99_latency_ms < cfg.timeout_ms ?
      std::pow((cfg.timeout_ms - summary.p99_latency_ms), 2) / 100 : 0;
    BOOST_LOG_TRIVIAL(info) << "stat.p99_latency_ms " << stat.p99_latency_ms << " cfg.timeout_ms " << cfg.timeout_ms
      << " qps " << qps << " response_time_score " << stat.response_time_score;
    return true;
}

bool TestServiceStabilityCalcStat(std::vector<std::string> services, int32_t max_qps, Statistic& stat) {
    auto& cfg = g_config.stability_cfg;
    auto reader = TestCaseReaderPreload(cfg.test_case_csv, cfg.csv_reader_capacity);
    reader.Start();

    cfg.max_qps = max_qps;
    BOOST_LOG_TRIVIAL(info) << "TestServiceStabilityScore ";
    auto summary = TestServiceStabilityScore(services, reader, cfg);
    reader.Stop();

    stat.service_score = 0;
    stat.timeout_percent = summary.timeout_request_count * 1.0 / summary.completed_requests;
    auto& custom_summary = summary.custom_summary;
    double avg_score = custom_summary.total_score / custom_summary.total_num;
    if (IsLess(avg_score, cfg.sample_score_th)) {
      return false;
    }

    if (summary.interupted || IsLess(summary.success_request_percent, cfg.success_percent_th)) {
      BOOST_LOG_TRIVIAL(info) << "service score " << stat.service_score;
      return false;
    }
    stat.service_score = 100;
    BOOST_LOG_TRIVIAL(info) << "service score " << stat.service_score;
    return true;
}