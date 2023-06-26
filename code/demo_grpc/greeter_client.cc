#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include <boost/lockfree/queue.hpp>
#include <grpcpp/grpcpp.h>
#include "helloworld.grpc.pb.h"
#include <etcd/Client.hpp>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using alimama::proto::Request;
using alimama::proto::Response;
using alimama::proto::SearchService;

class SearchClient {
 public:
  SearchClient(std::shared_ptr<Channel> channel) : stub_(SearchService::NewStub(channel)) {}

  void Search(const std::vector<uint64_t>& keywords, const std::vector<float>& context_vector, uint64_t hour, uint64_t topn) {
    Request request;

    for (auto keyword : keywords) {
      request.add_keywords(keyword);
    }

    for (auto value : context_vector) {
      request.add_context_vector(value);
    }

    request.set_hour(hour);
    request.set_topn(topn);

    Response response;
    ClientContext context;

    Status status = stub_->Search(&context, request, &response);

    if (status.ok()) {
      for (int i = 0; i < response.adgroup_ids_size(); i++) {
        // std::cout << "Adgroup ID: " << response.adgroup_ids(i) << ", Price: " << response.prices(i) << std::endl;
      }
    } else {
      std::cout << "RPC failed" << std::endl;
    }
  }

 private:
  std::unique_ptr<SearchService::Stub> stub_;
};

int main(int argc, char** argv) {
    // 创建一个etcd客户端
  etcd::Client etcd("http://etcd:2379");

  // 从etcd中获取服务地址
  auto response =  etcd.get("/services/searchservice").get();
  if (response.is_ok()) {
      std::cout << "Service connected successful.\n";
  } else {
      std::cerr << "Service connected failed: " << response.error_message() << "\n";
      return -1;
  }
  std::string server_address = response.value().as_string();
  std::cout << "server_address " << server_address << std::endl;
  SearchClient client(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

  std::vector<uint64_t> keywords = {1, 2, 3};
  std::vector<float> context_vector = {0.1f, 0.2f};
  uint64_t hour = 12;
  uint64_t topn = 10;

 // 创建多个线程并发发送search请求
  const int kNumThreads = 40;
  std::vector<std::thread> threads(kNumThreads);
  boost::lockfree::queue<double> latencies(1000);
  std::atomic<int> completed_requests{0};
  auto start_time = std::chrono::steady_clock::now();
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread([&]() {
      while (true) {
        auto start = std::chrono::steady_clock::now();
        client.Search(keywords, context_vector, hour, topn);
        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push(latency);
        completed_requests++;
      }
    });
  }

  // 每隔1秒钟统计一次QPS和平均延迟
  int num_requests = 0;
  double total_latency = 0;
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration<double>(end_time - start_time).count();
    int num_completed_requests = completed_requests.load();
    double qps = num_completed_requests / elapsed_time;
    double avg_latency = 0;
    if (!latencies.empty()) {
      double latency = 0;
      for (size_t i=0;i<100; i++) {
        bool sucess = latencies.pop(latency);
        if (!sucess) {
          break;
        }
        total_latency += latency;
        num_requests++;
      }
      avg_latency = total_latency / num_requests;
      while(latencies.pop(latency)) {}
      total_latency = 0;
      num_requests = 0;
    }
    std::cout << "QPS: " << qps << ", Average Latency: " << avg_latency << " ms\n";
  }

  // 等待所有线程结束
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
  }


  return 0;
}
