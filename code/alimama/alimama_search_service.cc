#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>
#include <memory>
#include <string>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <etcd/Client.hpp>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#ifdef BAZEL_BUILD
#include "examples/protos/alimama.grpc.pb.h"
#else
#include "alimama.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;

using alimama::proto::Request;
using alimama::proto::Response;
using alimama::proto::SearchService;

std::string getLocalIP() {
    struct ifaddrs *ifAddrStruct = NULL;
    void *tmpAddrPtr = NULL;
    std::string localIP;
    getifaddrs(&ifAddrStruct);
    while (ifAddrStruct != NULL) {
        if (ifAddrStruct->ifa_addr->sa_family == AF_INET) {
        tmpAddrPtr = &((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
        char addressBuffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
        std::string interfaceName(ifAddrStruct->ifa_name);
        if (interfaceName == "en0" || interfaceName == "eth0") {
            return addressBuffer;
        }
        }
        ifAddrStruct = ifAddrStruct->ifa_next;
    }
    return "";
}

struct Options {
    int node_id;
    int node_num;
    int cpu;
    uint64_t mempry;
};

std::ostream& operator<<(std::ostream& os, Options& options) {
    os << "node information: {"  <<  std::endl;
    os << '\t' << "node_id: " << options.node_id << std::endl
        << '\t' << "node_num: " << options.node_num << std::endl
        << '\t' << "mempry: " << options.mempry << std::endl
        << '\t' << "cpu: " << options.cpu << std::endl;
    os << "}"<< std::endl;
    return os;
}

Options loadENV() {
    Options options;
    const char* env_p = std::getenv("NODE_ID");
    if(env_p)
        options.node_id = std::stoi(env_p) - 1;
    else{
        options.node_id = 0;
    }
    // std::cout<< "NODE_ID: " << options.node_id << std::endl;
    env_p = std::getenv("NODE_NUM");
    if(env_p)
        options.node_num = std::stoi(env_p);
    else{
        options.node_num = 1;
    }
    // std::cout<< "NODE_NUM: " << options.node_num << std::endl;
    env_p = std::getenv("MEMORY");
    if(env_p)
        options.mempry = std::stoull(env_p) << 30; //4G
    else{
        options.mempry = 2 << 30; //2G
    }
    // std::cout<< "MEMORY: " << options.mempry << std::endl;
    env_p = std::getenv("CPU");
    if(env_p != nullptr)
        options.cpu = std::stoi(env_p);
    else{
        options.cpu = 2;
    }
    // std::cout<< "CPU: " << options.cpu << std::endl;
    return options;
}

// ETCD 
const std::string ECTD_URL = "http://etcd:2379";
const std::string modelservice_key = "/services/searchservice";
const std::string filename = "/data/raw_data.csv";
const size_t initialSize = 1e5;

class SearchServiceImpl final : public SearchService::Service {
public:
    SearchServiceImpl(Options options, std::string server_address): 
        options_(options),
        server_address_(server_address),
        etcd_(ECTD_URL),
        hash_tables(initialSize),
        data_num_(0)
    {
            
    }

    ~SearchServiceImpl() {
        etcd_.rm(modelservice_key);
    }

    int init() {
        if(load_data() == -1) {
            std::cout << "fiald to load data " << filename << std::endl;
            return -1;
        }

        // for(auto & pair: hash_tables) {
        //     std::cout << "keyword:" << pair.first << std::endl;
        //     for(auto & data: pair.second) {
        //         std::cout  << "adgroup_id: " << data.adgroup_id << " | "
        //                 << "price: " << data.price << " | "
        //                 << "timings: " << std::hex << data.timings << std::dec << " | "
        //                 << "vector: ["<< data.vec1 << ", "<< data.vec2 << "]"
        //                 << std::endl;
        //     }
        //     std::cout <<std::endl;
        // }
        // 将服务地址注册到etcd中
        auto response = etcd_.set(modelservice_key, server_address_).get();
        if (response.is_ok()) {
            std::cout << "Service registration successful.\n";
        } else {
            std::cerr << "Service registration failed: " << response.error_message()
                    << "\n";
            return -1;
        }
        return 0;
    }

    Status Search(ServerContext *context, const Request *request,
                    Response *response) override {
        uint64_t hour = request->hour();
        float vec1 = request->context_vector(0);
        float vec2 = request->context_vector(1);
        // std::cout << "vec1: " << vec1 << "  vec2: " << vec2 << "\n"; 
        float dist = sqrt(vec1 * vec1 + vec2 * vec2);
        uint64_t topn = request->topn();

        // 1. search match result
        std::vector<RawData> searchDatas;
        std::vector<int> indexs;
        int j = 0;
        for(int i = 0; i < request->keywords_size(); i++) {
            auto keyword = request->keywords(i);
            if(hash_tables.find(keyword) != hash_tables.end()) {
                for(auto & data: hash_tables[keyword]) {
                    if(data.timings & (1 << hour)) {
                        searchDatas.push_back(data);
                        indexs.push_back(j++);
                    }
                }
            }
        }

        // 2. 分数计算
        // 预估点击率 = 商品向量 和 用户_关键词向量 的余弦距离
        // 排序分数 = 预估点击率 x 出价（分数越高，排序越靠前）
        std::vector<std::pair<float, float>> scores(searchDatas.size());
        for(int i = 0; i < searchDatas.size(); i++) {
            auto & data = searchDatas[i];
            float up = data.vec1 * vec1 + data.vec2 * vec2;
            float down = sqrt(data.vec1 * data.vec1 + data.vec2 * data.vec2) * dist;
            scores[i].first = up / down + 0.000001f;
            scores[i].second = scores[i].first * data.price;

            // std::cout << "vec1: " << data.vec1 
            //     << "  vec2: " << data.vec2 
            //     << " price: " <<data.price << "\n"; 
            // std::cout << "预估点击率: " << scores[i].first << " | "
            //         << "排序分数: " << scores[i].second << "\n";
        }

        // 3. top_k
        std::sort(indexs.begin(), indexs.end(), [&scores] (int &l, int r) {
            return scores[l].second > scores[r].second;
        });

        // 4. prices
        // 计费价格（计费价格 = 第 i+1 名的排序分数 / 第 i 名的预估点击率（i表示排序名次，例如i=1代表排名第1的广告））
        std::vector<uint64_t> prices(searchDatas.size());
        for(int i = 0; i < indexs.size(); i++) {
            auto score = scores[indexs[i]];
            int pre_idx = i == indexs.size() - 1 ? i : i + 1;
            prices[i] = scores[indexs[pre_idx]].second / score.first;
        }

        // 5. fill result
        int n = indexs.size() < topn ? indexs.size() : topn;
        for(int i = 0; i < n; i++) {
            int idx = indexs[i];
            response->add_adgroup_ids(searchDatas[idx].adgroup_id);
            response->add_prices(prices[i]);
            std::cout << searchDatas[idx].adgroup_id << "  " << prices[i] << "\n";
        }
        return Status::OK;
    }

private:
    int load_data() {
        std::ifstream csv_file(filename);

        if (!csv_file.is_open()) {
            std::cout << "fiald to open " << filename << std::endl;
            return -1;
        }

        int64_t key_word = 0;
        int status = 0; 
        std::string line, cell;
        while (std::getline(csv_file, line)) {
            std::istringstream iss(line);
            RawData rawData;
            // keywrod
            iss >> cell;
            key_word = std::stoull(cell);
            // adgroup_id
            iss >> cell;
            rawData.adgroup_id = std::stoull(cell);
            // price
            iss >> cell;
            rawData.price = std::stoul(cell);
            // status
            iss >> cell;
            status = std::stoi(cell);
            if(status == 0)
                continue;
            // timings
            iss >> cell;
            rawData.timings = 0;
            int k = 0;
            for(int i = 0; i < cell.size(); i++) {
                if(cell[i] == '0') {
                    k++;
                }else if(cell[i] == '1') {
                    rawData.timings |= 1 << k;
                    k++;
                }
            }
    
            iss >> cell;
            int pos = cell.find_first_of(',');
            rawData.vec1 = std::stof(cell.substr(0, pos));
            rawData.vec2 = std::stof(cell.substr(pos + 1));
    
            if(hash_tables.find(key_word) == hash_tables.end()) {
                hash_tables[key_word] = {};
            }
            hash_tables[key_word].push_back(rawData);
            data_num_++;
        }
        return 0;
    }

private:
    Options options_;
    std::string server_address_;
    etcd::Client etcd_;

    struct RawData {
        uint64_t adgroup_id;
        uint16_t price;
        int32_t timings;
        float vec1, vec2;
    };

    std::unordered_map<int64_t, std::vector<RawData>> hash_tables;
    uint64_t data_num_;
};

void RunServer() {
    Options options = loadENV();
    if(options.node_id != 0)
        return;
    std::string server_address = getLocalIP() + ":50051";
    std::cout << options << std::endl;

    SearchServiceImpl service(options, server_address);
    if(service.init() != 0) {
        std::cout << "SearchService faild to init ...." << std::endl;
        return ;
    }

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
}

int main(int argc, char **argv) {
  RunServer();

  return 0;
}