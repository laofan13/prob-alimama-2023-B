#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>

#include <cmath>
#include <etcd/Client.hpp>
#include <boost/lockfree/queue.hpp>
#include <grpcpp/grpcpp.h>

#include "grpc_benchmark.h"

#include "alimama.grpc.pb.h"

#include "defines.h"
#include "config.h"
#include "test_cases.h"

TestConfig g_config;



std::vector<std::string> SetupSearchService() {
  std::vector<std::string> services{};
  etcd::SyncClient etcd("http://etcd:2379");
  const std::string kPublicEndpoint = "/services/searchservice";

  bool found {false};
  std::string server_address {};

  std::chrono::seconds timeout(g_config.loading_time_sec_timeout);
  std::chrono::seconds interval(5);
  std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();

  // wait until timeout
  while (std::chrono::system_clock::now() - start < timeout) {
    auto resp = etcd.get(kPublicEndpoint);
    if (!resp.is_ok()) {
      BOOST_LOG_TRIVIAL(info) << "get key fail. key: " << kPublicEndpoint << ", err: " << resp.error_message();
      std::this_thread::sleep_for(interval);
      continue;
    }
    found = true;
    server_address = resp.value().as_string();
    break;
  }
  if (!found) return services;
  std::chrono::time_point<std::chrono::system_clock> end = std::chrono::system_clock::now();
  std::chrono::duration<double> diff = end - start;
  BOOST_LOG_TRIVIAL(info) << "etcd connected successful. use " << diff.count() << " second";

  for (size_t i = 0; i < 1; i++) {
    BOOST_LOG_TRIVIAL(info)  << "found server_address " << server_address;
    services.push_back(server_address);
  }
  return services;
}

void Command(std::string command) {
  BOOST_LOG_TRIVIAL(info)  << "command: " << command;
	system(command.c_str());
}

// // WRONG_ANSWER(4, "答案错误", "您提交的程序没有通过所有的测试用例"),
// // ACCEPTED(5, "答案正确", "恭喜！您提交的程序通过了所有的测试用例"),
// // TIME_LIMIT_EXCEEDED(6, "运行超时", "您的程序未能在规定时间内运行结束，请检查是否循环有错或算法复杂度过大。"),
// // EXECUTION_COMPLETED(28, "运行成功", "代码运行成功"),
void DumpStats(Statistic& stat) {
  json j;
  j["code"] = 28;
  j["extras"]["result_score"] = stat.result_score;
  j["extras"]["response_time_score"] = stat.response_time_score;
  j["extras"]["capacity_score"] = stat.capacity_score;
  j["extras"]["service_score"] = stat.service_score;
  j["extras"]["final_score"] = stat.final_score;
  j["extras"]["p99_latency_ms"] = stat.p99_latency_ms;
  j["extras"]["max_qps"] = stat.max_qps;
  j["extras"]["M"] = stat.M;
  j["extras"]["score"]  = stat.final_score;
  j["extras"]["last_timeout_percent"]  = stat.timeout_percent;
  j["score"] = stat.final_score;
  std::ofstream of("./testbenchResult.json", std::ios::out);
  of << j.dump();
  of.close();
  BOOST_LOG_TRIVIAL(info)  << "得分情况：: " << j.dump();
}

bool CheckStubs(StubsVector& stubs) {
  if (stubs.size() == 0) {
    BOOST_LOG_TRIVIAL(error)  << "failed to setup serach service ";
  }
  for(auto& stub: stubs) {
    if (!stub) {
      BOOST_LOG_TRIVIAL(error)  << "failed to setup serach service , got nullptr ";
      return false;
    }
  }
  return true;
}

void TestAll(Statistic& stat) {
  BOOST_LOG_TRIVIAL(trace)  << "setup service";
  auto services = SetupSearchService();
  if (services.size() == 0) return;

  if (!TestResulCalcStat(services, stat)) return;
  if (!TestMaxQpsCalcStat(services, stat)) return;
  // if (!TestCapacityCalcStat(services, stat)) return;
  if (!TestResponseTimeCalcStat(services, stat)) return;
  if (!TestServiceStabilityCalcStat(services, stat.max_qps, stat)) return;
}

void InitLogging() {
  logging::core::get()->set_filter(logging::trivial::severity >= logging::trivial::info);
}

int main(int argc, char** argv) {
  InitLogging();

  const std::string kConfigFilePath("config.json");
  bool ok = ReadConfigFromFile(kConfigFilePath, g_config);
  if (!ok) {
    return -1;
  }

  Statistic stat{};
  TestAll(stat);
  stat.final_score = 0.5 * stat.result_score + 0.2 * stat.response_time_score + 0.2 * stat.capacity_score + 0.1 * stat.service_score;
  DumpStats(stat);
  return 0;
}
