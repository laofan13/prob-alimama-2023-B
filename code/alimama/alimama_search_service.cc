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
using grpc::Channel;

using alimama::proto::Request;
using alimama::proto::Response;
using alimama::proto::SearchResponse;
using alimama::proto::SearchService;

#include "alimama.pb.h"
#include "config.h"
#include "utils.h"

class SearchServiceImpl final : public SearchService::Service {
public:
    SearchServiceImpl(Options options, std::string server_address): 
        options_(options),
        server_address_(server_address),
        etcd_(ECTD_URL)
    {   

    }

    ~SearchServiceImpl() {
        if(adgroupID_datas)
            delete adgroupID_datas;
        if(keyword_datas)
            delete keyword_datas;
    }

    int init() {
        // alloc memory
        adgroupID_datas = new AdgroupUnit[adgroup_id_num];
        keyword_datas = new KeyWrodUnit[total_data_num];

        // 加载广告数据
        auto start = std::chrono::high_resolution_clock::now();
        if(load_csv_data() != 0) {
            std::cout << "fiald to load data " << filename << "\n";
            return -1;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto load_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        std::cout << "加载文件所花费的时间：" << load_duration << " 毫秒" << "\n";
        std::cout << "文件行数：" << keyword_num << "\n";

        // // 广告数据
        // std::cout << "广告单元：" << "\n";
        // for(int i = 0; i < adgroupID_num; i++) {
        //     std::cout << i << " | " << adgroupID_datas[i] << "\n";
        // }
        // 排序
        start = std::chrono::high_resolution_clock::now();
        std::sort(keyword_datas, keyword_datas + keyword_num, [] (const KeyWrodUnit & l, const KeyWrodUnit & r) {
            return l.key_word < r.key_word;
        });
        end = std::chrono::high_resolution_clock::now();
        auto sort_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        std::cout << "对数据进行排序所花费的时间：" << sort_duration << " 毫秒" << "\n";

        // 关键词数据
        // for(int64_t i = 0; i < keyword_num; i++)
        //     std::cout << keyword_datas[i] << "\n";
            
        // 构建索引
        start = std::chrono::high_resolution_clock::now();

        keyword_indexs.reserve(key_word_num);
        int64_t s = 0;
        for(int64_t i = 1; i < keyword_num; i++) {
            if(keyword_datas[i].key_word != keyword_datas[i-1].key_word) {
                keyword_indexs[keyword_datas[s].key_word] = {s, i-1};
                s = i;
            }
        }
        keyword_indexs[keyword_datas[s].key_word] = {s, keyword_num - 1};

        end = std::chrono::high_resolution_clock::now();
        auto index_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        std::cout << "构建索引所花费的时间：" << index_duration << " 毫秒" << "\n";

        // for(auto & it: keyword_indexs) {
        //     std::cout << it.first << " = [" << it.second.first << "," <<  it.second.second << "]"
        //         << "\n";
        // }
        
        // 将服务地址注册到etcd中
        std::string statistics = "|" +std::to_string(keyword_num) + 
                    "|" + std::to_string(adgroupID_num);
                    "|" + std::to_string(keyword_indexs.size());
        auto response = etcd_.set(modelservice_key, server_address_ + statistics).get();
        if (response.is_ok()) {
            std::cout << "Service registration successful.\n";
        } else {
            std::cerr << "Service registration failed: " << response.error_message()
                    << "\n";
            return -1;
        }
        return 0;
    }

    int load_csv_data() {
        std::ifstream csv_file(filename);
        char buf[65536];
        csv_file.rdbuf()->pubsetbuf(buf, 65536);

        if (!csv_file.is_open()) {
            std::cout << "fiald to open " << filename << "\n";
            return -1;
        }

        // index
        std::unordered_map<uint64_t, uint32_t> adgroupID_map_indexs;
        adgroupID_map_indexs.reserve(adgroup_id_num);

        std::string line;
        RawData data;
        while (std::getline(csv_file, line)) {
            if(!parserRawData(options_, line, data))
                continue;

            uint32_t adgroup_id_idx = 0;
            if(adgroupID_map_indexs.find(data.adgroup_id) == adgroupID_map_indexs.end()) {
                adgroupID_datas[adgroupID_num] = AdgroupUnit(data.adgroup_id, data.item_vec1, data.item_vec2, data.timings_mask);
                adgroupID_map_indexs[data.adgroup_id] = adgroupID_num;
                adgroup_id_idx = adgroupID_num++;
            }else{
                adgroup_id_idx = adgroupID_map_indexs[data.adgroup_id];
            }

            keyword_datas[keyword_num] = KeyWrodUnit(data.keyword, adgroup_id_idx ,data.price);
            keyword_num++;
        }
        // 关闭文件
        csv_file.close();
        return 0;
    }
    
    std::vector<SearchResult> recall(const Request *request) {
        uint64_t hour = request->hour();
        float context_vec1 = request->context_vector(0);
        float context_vec2 = request->context_vector(1);
        float context_dist = sqrt(context_vec1 * context_vec1 + context_vec2 * context_vec2);

        // 1. search match result
        std::vector<SearchResult> search_results;
        for(int i = 0; i < request->keywords_size(); i++) {
            // Recall
            auto keyword = request->keywords(i);
            // std::cout << "keyword=" << keyword << std::endl;
            if(keyword % options_.node_num != options_.node_id)
                continue;
            if(keyword_indexs.find(keyword) == keyword_indexs.end()) 
                continue;
            auto matchResult = keyword_indexs[keyword];
            
            // 计算预估点击率 和分数
            for(int64_t i = matchResult.first; i <= matchResult.second; i++ ) {
                KeyWrodUnit & key_word_data = keyword_datas[i];
                AdgroupUnit & Adgroup_data = adgroupID_datas[key_word_data.adgroup_id_idx];
                // Filtering
                if(!(Adgroup_data.timings_mask & (1 << hour))) 
                    continue;

                float up = Adgroup_data.item_vec1 * context_vec1 + Adgroup_data.item_vec2 * context_vec2;
                float down = sqrt(Adgroup_data.item_vec1 * Adgroup_data.item_vec1 + Adgroup_data.item_vec2 * Adgroup_data.item_vec2) * context_dist;
                // 预估点击率 = 商品向量 和 用户_关键词向量 的余弦距离
                float ctr = up / down + 0.000001f;
                // 排序分数 = 预估点击率 x 出价（分数越高，排序越靠前）
                float score = ctr * key_word_data.price;

                search_results.emplace_back(Adgroup_data.adgroup_id, key_word_data.price, ctr, score);
            }
        }
        if(search_results.empty())
            return {};

        // 2.分数越高，排序越靠前
        // 若排序过程中排序分数相同，选择出价低者，出价相同，adgroup_id大的排前面。
        std::sort(search_results.begin(), search_results.end(), order_cmp);

        // 去重

        // 4. prices
        // 计费价格（计费价格 = 第 i+1 名的排序分数 / 第 i 名的预估点击率（i表示排序名次，例如i=1代表排名第1的广告））
        int n = search_results.size();
        for(int i = 0; i < n - 1; i++) {
            search_results[i].bill_price = std::round(search_results[i+1].score / search_results[i].ctr);
        }
        search_results[n - 1].bill_price = search_results[n - 1].price;

        return std::move(search_results);
    }


    Status Search(ServerContext *context, const Request *request,
                    Response *response) override {
        
        uint64_t topn = request->topn();
        auto final_results = recall(request);

        // std::cout << "final_results: " << "\n";
        // for(auto & data: final_results)
        //     std::cout << data << "\n";

        int n = final_results.size() <= topn ? final_results.size() : topn;
        response->mutable_adgroup_ids()->Reserve(n);
        response->mutable_prices()->Reserve(n);
        for(int i = 0; i < n; i++) {
            response->add_adgroup_ids(final_results[i].adgroup_id);
            response->add_prices(final_results[i].bill_price);
            // std::cout << results[i].adgroup_id << "  " << results[i].bill_price << "\n";
        }
        return Status::OK;
    }

private:
    Options options_;
    std::string server_address_;
    etcd::Client etcd_;

    // adgroup_id
    AdgroupUnit* adgroupID_datas;
    int64_t adgroupID_num = 0;

    //data
    KeyWrodUnit* keyword_datas;
    int64_t keyword_num = 0;

    // index
    std::unordered_map<uint64_t, std::pair<int64_t, int64_t>> keyword_indexs;
};

void RunServer() {
    Options options = loadENV();
    if(options.node_id != 0) {
        std::cout << "node_" << options.node_id <<" exit ..." << "\n";
        return;
    }
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
//   testService();
  return 0;
}
