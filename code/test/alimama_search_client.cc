#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

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
                std::cout << "Adgroup ID: " << response.adgroup_ids(i) << ", Price: " << response.prices(i) << std::endl;
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

    // 4803367238	0.552321,0.833632	20	1	722542970812	17934
    {
        std::vector<uint64_t> keywords = {4803367238};
        std::vector<float> context_vector = {0.552321f, 0.833632f};
        uint64_t hour = 20;
        uint64_t topn = 1;
        client.Search(keywords, context_vector, hour, topn);
    }

    {
        std::vector<uint64_t> keywords = {4803367239};
        std::vector<float> context_vector = {0.552321f, 0.833632f};
        uint64_t hour = 20;
        uint64_t topn = 1;
        client.Search(keywords, context_vector, hour, topn);
    }
    
    //12210106372	0.975501,0.219997	16	3	1804714034430	41953
    {
        std::vector<uint64_t> keywords = {12210106372};
        std::vector<float> context_vector = {0.975501f, 0.219997f};
        uint64_t hour = 16;
        uint64_t topn = 1;
        client.Search(keywords, context_vector, hour, topn);
    }

    {
        std::vector<uint64_t> keywords = {12210106373};
        std::vector<float> context_vector = {0.975501f, 0.219997f};
        uint64_t hour = 16;
        uint64_t topn = 1;
        client.Search(keywords, context_vector, hour, topn);
    }
 
  return 0;
}
