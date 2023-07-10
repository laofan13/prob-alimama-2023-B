#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <random>
#include <limits>

#include <cmath>
#include <etcd/Client.hpp>
#include <boost/lockfree/queue.hpp>
#include <grpcpp/grpcpp.h>
#include "alimama.grpc.pb.h"

#include "grpc_benchmark.h"
#include "test_case_reader.h"
#include "test_case_reader_preload.h"
#include "test_case_reader_async.h"
#include "config.h"
#include "concurent_queue.h"

#include "search_service_async_client.h"
#include "search_service_sync_client.h"
#include "search_service_comparator.h"

#include "defines.h"
#include "utils.h"
#include "timer.h"

void DumpSummary(const SearchServiceGprcBenchmark::SummaryData& summary) {
  BOOST_LOG_TRIVIAL(info)  << "summary completed_requests " << summary.completed_requests;
  BOOST_LOG_TRIVIAL(info)  << "summary avg_latency_ms " << summary.avg_latency_ms;
  BOOST_LOG_TRIVIAL(info)  << "summary p99_latency_ms " << summary.p99_latency_ms;
  BOOST_LOG_TRIVIAL(info)  << "summary interupted " << summary.interupted;
  BOOST_LOG_TRIVIAL(info)  << "summary qps " << summary.custom_summary.qps;
  BOOST_LOG_TRIVIAL(info)  << "summary error_request_count " << summary.error_request_count;
  BOOST_LOG_TRIVIAL(info)  << "summary success_request_count " << summary.success_request_count;
  BOOST_LOG_TRIVIAL(info)  << "summary success_request_percent " << summary.success_request_percent;
  BOOST_LOG_TRIVIAL(info)  << "summary timeout_request_count " << summary.timeout_request_count;
  BOOST_LOG_TRIVIAL(info)  << std::endl;
}

SearchServiceGprcBenchmark::SummaryData RunBenchmark(
    std::shared_ptr<SearchServiceGprcBenchmark> bench,
    TestCaseReader& reader, int32_t req_times, int32_t timeout_sec, bool enable_repeat=true) {
  Timer tm{};
  if (req_times == 0) req_times = std::numeric_limits<int>::max();
  for(size_t i=0; i<req_times; i++) {
    TestCasePair pair{};
    if (!reader.Pop(pair)) {
      BOOST_LOG_TRIVIAL(error)  << "pop data failed";
      break;
    }
    if (!enable_repeat && pair.repeat) {
      BOOST_LOG_TRIVIAL(info)  << "all test cases have been read";
      break;
    }
    if (!bench->Request(i, pair.req, pair.response)) {
      BOOST_LOG_TRIVIAL(error)  << "request data failed";
      break;
    }
  }
  tm.Stop();
  if (timeout_sec == 0) {
    bench->WaitAll();
  } else {
    bench->WaitAllUntilTimeout(timeout_sec * 1000 - int32_t(tm.ElapsedMillisec()));
  }
  tm.Stop();

  auto summary = bench->Summary();
  summary.custom_summary.qps = summary.success_request_count / tm.ElapsedSec();
  DumpSummary(summary);
  return summary;
}

SearchServiceGprcBenchmark::SummaryData TestResultScore(std::vector<std::string> services, TestCaseReader& reader,
    const TestResultConfig& cfg) {
  auto clis = SearchServiceGprcSyncClient::CreateClientsByNum(services, cfg.thread_num);
  if (clis.size() == 0) return {};

  auto comparator = std::make_shared<SearchServiceComparatorAll>();
  auto bench = std::make_shared<SearchServiceGprcBenchmark>(clis, comparator, cfg.timeout_ms, cfg.qps_limit);
  return RunBenchmark(bench, reader, cfg.sample_num, 0);
}

SearchServiceGprcBenchmark::SummaryData TestMaxQps(std::vector<std::string> services, TestCaseReader& reader, const TestMaxQpsConfig& cfg) {
  double qps_limit = cfg.qps_baseline;
  double qps_max = cfg.qps_upper;

  auto doBatchBench = [&qps_limit, &qps_max, &cfg, &services, &reader](int32_t step) -> SearchServiceGprcBenchmark::SummaryData {
    SearchServiceGprcBenchmark::SummaryData valid_summary{};
    while(qps_limit<=qps_max) {
      auto clis = SearchServiceGprcClient::CreateClients(services, int32_t(qps_limit));
      if (clis.size() == 0) return valid_summary;

      auto comparator = std::make_shared<SearchServiceComparatorSample>(cfg.sample_percent_th, cfg.sample_score_th);
      auto bench = std::make_shared<SearchServiceGprcBenchmark>(clis, comparator, cfg.timeout_ms, int32_t(qps_limit));
      int64_t request_times = qps_limit * cfg.request_duration_each_iter_sec;

      auto summary = RunBenchmark(bench, reader, request_times, cfg.request_duration_each_iter_sec);

      auto& user_summary = summary.custom_summary;
      double avg_score = user_summary.total_score / user_summary.total_num;

      if (summary.success_request_percent < cfg.success_percent_th || IsLess(avg_score, cfg.success_percent_th)) {
        qps_max = qps_limit;
        qps_limit = qps_max - step;
        break;
      }
      qps_limit += step;
      valid_summary = summary;
    }
    return valid_summary;
  };

  auto summary0 = doBatchBench(cfg.qps_step0);
  DumpSummary(summary0);

  if (int32_t(qps_limit) <= 0) {
    qps_limit = cfg.qps_step1;
  }

  std::this_thread::sleep_for(std::chrono::seconds(10));

  auto summary1 = doBatchBench(cfg.qps_step1);
  DumpSummary(summary1);
  std::this_thread::sleep_for(std::chrono::seconds(10));

  if (summary1.success_request_percent < cfg.success_percent_th) return summary0;
  return summary1;
}

SearchServiceGprcBenchmark::SummaryData TestResponseTime(std::vector<std::string> services, TestCaseReader& reader, const TestResponseTimeConfig& cfg) {
  auto clis = SearchServiceGprcClient::CreateClients(services, cfg.qps_limit);
  if (clis.size() == 0) return {};

  auto comparator = std::make_shared<SearchServiceComparatorSample>(cfg.sample_percent_th, cfg.sample_score_th);
  auto bench = std::make_shared<SearchServiceGprcBenchmark>(clis, comparator, cfg.timeout_ms, cfg.qps_limit);
  
  int32_t req_times = cfg.qps_limit * cfg.test_duration_sec;
  return RunBenchmark(bench, reader, req_times, cfg.test_duration_sec, false);
}

SearchServiceGprcBenchmark::SummaryData TestServiceStabilityScore(std::vector<std::string> services, TestCaseReader& reader, const TestStabilityConfig& cfg) {
  const auto QPS_LIMIT = cfg.load_percent * cfg.max_qps;
  auto clis = SearchServiceGprcClient::CreateClients(services, QPS_LIMIT);
  if (clis.size() == 0) return {};

  auto comparator = std::make_shared<SearchServiceComparatorSample>(cfg.sample_percent_th, cfg.sample_score_th);
  auto bench = std::make_shared<SearchServiceGprcBenchmark>(clis, comparator, cfg.timeout_ms, QPS_LIMIT);
  
  int32_t req_times = QPS_LIMIT * cfg.test_duration_sec;
  return RunBenchmark(bench, reader, req_times, cfg.test_duration_sec);
}

