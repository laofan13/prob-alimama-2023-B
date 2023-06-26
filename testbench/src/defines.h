#pragma once

#include <grpcpp/impl/codegen/status.h>
#include <grpcpp/impl/codegen/client_context.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "alimama.grpc.pb.h"

#include "log.h"
#include "config.h"
#include "grpc_benchmark.h"

using grpc::Status;
using grpc::ClientContext;
using grpc::Channel;

using std::shared_ptr;

using RequestPtr = std::shared_ptr<alimama::proto::Request>;
using ResponsePtr = std::shared_ptr<alimama::proto::Response>;
using alimama::proto::SearchService;
using alimama::proto::Request;
using alimama::proto::Response;

extern TestConfig g_config;

struct CustomSummary{
  double qps;
  int32_t total_num;
  double total_score;
  int32_t ad_correct_num; // 集合和顺序都正确
  int32_t ad_partial_correct_num; // 集合正确，顺序不正确
  int32_t price_correct_num; // 价格正确
};

using StubsVector=std::vector<std::unique_ptr<SearchService::Stub>>;
using SearchServiceGprcBenchmark = GrpcBenchmark<RequestPtr, ResponsePtr, CustomSummary, ResponsePtr>;
using GrpcClientPtr = shared_ptr<GrpcClient<RequestPtr, ResponsePtr>>;

struct RequestItem {
  std::shared_ptr<ClientContext> ctx;
  void* obj;
  RequestPtr req;
  ResponsePtr resp;
  std::shared_ptr<Status> status;
};