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

#include "grpc_benchmark.h"
#include "test_case_reader.h"
#include "test_case_reader_preload.h"
#include "test_case_reader_async.h"
#include "config.h"
#include "concurent_queue.h"
#include "test_search_service.h"

#include "defines.h"

class SearchServiceGprcClient : public GrpcClient<RequestPtr, ResponsePtr> {
private:
  StubsVector stubs_;
  std::vector<std::string> services_;
  shared_ptr<grpc::CompletionQueue> cq_;
  std::atomic<uint64_t> req_idx_;

  std::mutex mtx_;
  bool enable_;
public:
  static std::vector<GrpcClientPtr> CreateClients(const std::vector<std::string>& services, int32_t qps_limit) {
    std::vector<GrpcClientPtr> clients{};
    constexpr int32_t kDefaultQpsEachThread{4000};
    auto cli_num = (qps_limit / kDefaultQpsEachThread) + 1;
    for(size_t i=0; i<cli_num; i++) {
      auto cli = std::make_shared<SearchServiceGprcClient>(services);
      if (!cli->Init()) {
        BOOST_LOG_TRIVIAL(error)  << "init clients failed";
        return std::vector<GrpcClientPtr>{};
      }
      clients.push_back(cli);
    }
    return clients;
  };

  SearchServiceGprcClient(const std::vector<std::string> services):
    services_(services), req_idx_{0},enable_{false},cq_() {};
  ~SearchServiceGprcClient() {};

  bool Init() {
    for (const auto& svc : services_) {
      std::unique_ptr<SearchService::Stub> stub(SearchService::NewStub(grpc::CreateChannel(svc, grpc::InsecureChannelCredentials())));
      if (!stub) {
        BOOST_LOG_TRIVIAL(error)  << "failed to setup serach service , got nullptr ";
        return false;
      }
      stubs_.push_back(std::move(stub));
      auto q = std::make_shared<grpc::CompletionQueue>();
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
    std::unique_ptr<grpc::ClientAsyncResponseReader<Response> > rpc(
      stubs_[idx]->AsyncSearch(ctx.get(), *req, cq_.get()));
    ResponsePtr resp = std::make_shared<Response>();
    auto status = std::make_shared<Status>();
    auto* item = new RequestItem{ctx, obj, req, resp, status};
    rpc->Finish(resp.get(), status.get(), (void*)item);
    return true;
  }

  bool WaitResponse(ResponsePtr& resp, std::shared_ptr<Status>& status, void** obj) {
    void * got;
    bool ok = false;
    bool success = cq_->Next(&got, &ok);
    if (!success) {
      return false;
    }
    if (!ok) {
      BOOST_LOG_TRIVIAL(warning) << "request !OK";
      // TODO
      return false;
    }
    RequestItem* got_item = (RequestItem*) got;
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
    cq_->Shutdown();
    enable_ = false;
    return true;
  }

  SearchServiceGprcClient(const SearchServiceGprcClient&) = delete;
  SearchServiceGprcClient(SearchServiceGprcClient&&) = delete;
  SearchServiceGprcClient& operator=(const SearchServiceGprcClient&) = delete;
  SearchServiceGprcClient& operator=(SearchServiceGprcClient&&) = delete;
};
