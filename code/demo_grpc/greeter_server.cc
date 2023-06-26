/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <etcd/Client.hpp>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerCompletionQueue;

using alimama::proto::Request;
using alimama::proto::Response;
using alimama::proto::SearchService;
class SearchServiceImpl final : public SearchService::Service {
  Status Search(ServerContext* context, const Request* request, Response* response) override {
    // 在这里实现您的广告匹配逻辑。
    // 作为示例，我们只是简单地返回一些假数据。

    // 假设我们找到了两个广告单元
    response->add_adgroup_ids(12345);
    response->add_adgroup_ids(67890);

    // 对应的价格
    response->add_prices(100);
    response->add_prices(200);

    return Status::OK;
  }
};

void RunServer() {
    const char* env_var = std::getenv("SERVICE_NAME");
    std::string env_var_str = std::string("0.0.0.0");
    if (env_var != nullptr) {
      env_var_str = std::string(env_var);
    } else {
      std::cout << "service " << env_var_str << std::endl;
    }
    std::cout << "Service registration starting.\n";

    std::string server_address = env_var_str + std::string(":50051");
    SearchServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());

      // 创建一个etcd客户端
    etcd::Client etcd("http://etcd:2379");

    // 将服务地址注册到etcd中
    auto response = etcd.set("/services/searchservice/node1:50051", server_address).get();
    if (response.is_ok()) {
        std::cout << "Service registration successful.\n";
    } else {
        std::cerr << "Service registration failed: " << response.error_message() << "\n";
    }

    std::cout << "Server listening on " << server_address  << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
  RunServer();

  return 0;
}
