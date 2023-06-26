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
#include "test_search_service.h"

#include "defines.h"

class SearchServiceGprcSyncClient : public GrpcClient<RequestPtr, ResponsePtr> {
private:
  StubsVector stubs_;
  std::vector<std::string> services_;
  shared_ptr<ConcurrentQueue<RequestItem*>> cq_;
  std::atomic<uint64_t> req_idx_;

  std::mutex mtx_;
  bool enable_;
public:
  static std::vector<GrpcClientPtr> CreateClientsByNum(const std::vector<std::string>& services, int32_t cli_num) {
    std::vector<GrpcClientPtr> clients{};
    for(size_t i=0; i<cli_num; i++) {
      auto cli = std::make_shared<SearchServiceGprcSyncClient>(services);
      if (!cli->Init()) {
        BOOST_LOG_TRIVIAL(error)  << "init clients failed";
        return std::vector<GrpcClientPtr>{};
      }
      clients.push_back(cli);
    }
    return clients;
  };
    
  static std::vector<GrpcClientPtr> CreateClients(const std::vector<std::string>& services, int32_t qps_limit) {
    constexpr int32_t kDefaultQpsEachThread{4000};
    auto cli_num = (qps_limit / kDefaultQpsEachThread) + 1;
    return CreateClientsByNum(services, cli_num);
  };

  SearchServiceGprcSyncClient(const std::vector<std::string> services):
    services_(services), req_idx_{0},enable_{false},cq_() {};
  ~SearchServiceGprcSyncClient() {};

  bool Init() {
    for (const auto& svc : services_) {
      std::unique_ptr<SearchService::Stub> stub(SearchService::NewStub(grpc::CreateChannel(svc, grpc::InsecureChannelCredentials())));
      if (!stub) {
        BOOST_LOG_TRIVIAL(error)  << "failed to setup serach service , got nullptr ";
        return false;
      }
      stubs_.push_back(std::move(stub));
      auto q = std::make_shared<ConcurrentQueue<RequestItem*>>();
      cq_.swap(q);
    }
    enable_ = true;
    return true;
  }

  bool Request(std::shared_ptr<ClientContext> ctx, RequestPtr& req, void* obj) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!enable_) return false;
    auto idx = req_idx_.fetch_add(1);
    idx = idx % stubs_.size();
    ResponsePtr resp = std::make_shared<Response>();
    auto status = std::make_shared<Status>();
    auto* item = new RequestItem{ctx, obj, req, resp, status};
    *status = stubs_[idx]->Search(ctx.get(), *req, resp.get());
    auto push_stat = cq_->Push(item);
    if (push_stat == QueueStatus::Closed) {
      return false;
    }
    return true;
  }

  bool WaitResponse(ResponsePtr& resp, std::shared_ptr<Status>& status, void** obj) {
    RequestItem* got_item{};
    auto stat = cq_->WaitAndPop(got_item);
    if (stat == QueueStatus::Closed) {
      return false;
    }
    if (!got_item) {
      BOOST_LOG_TRIVIAL(warning) << "request !got_item";
      return false;
    }
    if (obj) {
      *obj = got_item->obj;
    }
    resp.swap(got_item->resp);
    status.swap(got_item->status);
    delete got_item;
    return true;
  }

  bool Close() {
    std::lock_guard<std::mutex> lock(mtx_);
    cq_->Close();
    enable_ = false;
    return true;
  }

  SearchServiceGprcSyncClient(const SearchServiceGprcSyncClient&) = delete;
  SearchServiceGprcSyncClient(SearchServiceGprcSyncClient&&) = delete;
  SearchServiceGprcSyncClient& operator=(const SearchServiceGprcSyncClient&) = delete;
  SearchServiceGprcSyncClient& operator=(SearchServiceGprcSyncClient&&) = delete;
};
