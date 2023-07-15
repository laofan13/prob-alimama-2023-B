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
#include <chrono>

#include "utils.h"
#include "config.h"

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

class SearchServiceImpl final : public SearchService::Service {
public:
    SearchServiceImpl(Options options, std::string server_address): 
        options_(options),
        server_address_(server_address),
        etcd_(ECTD_URL)
    {   
        
    }

    ~SearchServiceImpl() {
        etcd_.rm(modelservice_key);
        if(datas_)
            delete [] datas_;
    }

    int init() {
         // 记录开始时间
        auto start = std::chrono::high_resolution_clock::now();
        if(load_csv_data() != 0) {
            std::cout << "fiald to load data " << filename << std::endl;
            return -1;
        }
        //   // 记录结束时间
        auto end = std::chrono::high_resolution_clock::now();
        
        // 计算读取文件所花费的时间（以毫秒为单位）
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        // 输出读取的文件内容和所花费的时间
        std::cout << "读取文件所花费的时间：" << duration << " 毫秒" << std::endl;
        std::cout << "文件行数：" << data_num << std::endl;

        // 排序
        // start = std::chrono::high_resolution_clock::now();
        std::sort(datas_, datas_ + data_num, [] (const RawData & l, const RawData & r) {
            return l.adgroup_id < r.adgroup_id;
        });
        // end = std::chrono::high_resolution_clock::now();
        // duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        // std::cout << "排序数据所花费的时间：" << duration << " 毫秒" << std::endl;

        // for(auto & pair: hash_tables) {
        //     std::cout << "keyword:" << pair.first << std::endl;
        //     for(auto & data: pair.second) {
        //         std::cout  << "adgroup_id: " << data.adgroup_id << " | "
        //                 << "price: " << data.price << " | "
        //                 << "timings_mask: " << std::hex << data.timings_mask << std::dec << " | "
        //                 << "vector: ["<< data.vec1 << ", "<< data.vec2 << "]"
        //                 << std::endl;
        //     }
        //     std::cout <<std::endl;
        // }

        // 将服务地址注册到etcd中
        // std::string val = std::to_string(data_num) + "|" + std::to_string(datas.capacity());
        auto response = etcd_.set(modelservice_key, server_address_ ).get();
        if (response.is_ok()) {
            std::cout << "Service registration successful.\n";
        } else {
            std::cerr << "Service registration failed: " << response.error_message()
                    << "\n";
            return -1;
        }
        return 0;
    }

private:
    int load_csv_data() {// alloc memory
        datas_ = new RawData[total_data_num];

        std::ifstream csv_file(filename);
        char buf[65536];
        csv_file.rdbuf()->pubsetbuf(buf, 65536);

        if (!csv_file.is_open()) {
            std::cout << "fiald to open " << filename << std::endl;
            return -1;
        }

        int64_t key_word = 0;
        std::string line, cell;
        while (std::getline(csv_file, line)) {
            int n = line.size();
            int i = 0;
            key_word = 0;
            RawData & rawData = datas_[data_num];
            // keywrod
            while(i < n && line[i] != '\t') {
                key_word = key_word * 10 + (line[i] - '0');
                i++;
            }

            // load controll
            if(key_word % options_.node_num != options_.node_id) 
                continue;

            while(i < n && line[i] == '\t')
                i++; 
            // adgroup_id
            rawData.adgroup_id = 0;
            while(i < n && line[i] != '\t') {
                rawData.adgroup_id = rawData.adgroup_id * 10 + (line[i] - '0');
                i++;
            }
            while(i < n && line[i] == '\t')
                i++;

            // price
            rawData.price = 0;
            while(i < n && line[i] != '\t') {
                rawData.price = rawData.price * 10 + (line[i] - '0');
                i++;
            }
            while(i < n && line[i] == '\t')
                i++;

            // status
            if(line[i] == '0') {
                continue;
            }
            i++;
            while(i < n && line[i] == '\t')
                i++;

            // timings
            rawData.timings_mask = 0;
            int k = 0;
            while(i < n) {
                if(line[i] == '0') {
                    k++;
                }else if(line[i] == '1') {
                    rawData.timings_mask |= 1 << k;
                    k++;
                }
                i++;
                if(k >= 24) 
                    break;
            }
            while(i < n && line[i] == '\t')
                i++;
            // vector1
            rawData.vec1 = 0.0f;
            float decimal = 0.1f;
            while (i < n && line[i] != '.') {
                rawData.vec1 = rawData.vec1 * 10.0f + (line[i] - '0');
                ++i;
            }
            ++i;
            while (i < n && line[i] != ',') {
                rawData.vec1 = rawData.vec1 + (line[i] - '0') * decimal;
                decimal *= 0.1f;
                ++i;
            }
            i++;
            // vector1
            rawData.vec2 = 0.0f;
            decimal = 0.1f;
            while (i < n && line[i] != '.') {
                rawData.vec2 = rawData.vec2 * 10.0f + (line[i] - '0');
                ++i;
            }
            ++i;
            while (i < n && line[i] != '\t') {
                rawData.vec2 = rawData.vec2 + (line[i] - '0') * decimal;
                decimal *= 0.1f;
                ++i;
            }

            data_num++;
        }

        // 关闭文件
        csv_file.close();
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
        // for(int i = 0; i < request->keywords_size(); i++) {
        //     auto keyword = request->keywords(i);
        //     if(hash_tables.find(keyword) != hash_tables.end()) {
        //         for(auto & data: hash_tables[keyword]) {
        //             if(data.timings_mask & (1 << hour)) {
        //                 searchDatas.push_back(data);
        //                 indexs.push_back(j++);
        //             }
        //         }
        //     }
        // }

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
    Options options_;
    std::string server_address_;
    etcd::Client etcd_;

    //byte size = 20B
    struct RawData {
        uint64_t adgroup_id;
        uint16_t price;
        int32_t timings_mask;
        float vec1, vec2;
    };

    RawData* datas_;
    uint64_t data_num = 0;
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
