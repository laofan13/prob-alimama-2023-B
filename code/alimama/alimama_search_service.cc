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

#include "config.h"
#include "utils.h"

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
        if(raw_datas)
            delete [] raw_datas;
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
        auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        // 输出读取的文件内容和所花费的时间
        std::cout << "读取文件所花费的时间：" << duration1 << " 毫秒" << std::endl;
        std::cout << "文件行数：" << data_num << std::endl;

        // // 打印数据
        // for(int i = 0; i < data_num; i++) {
        //     std::cout << raw_datas[i] << std::endl;
        // }

        // 排序
        start = std::chrono::high_resolution_clock::now();
        std::sort(raw_datas, raw_datas + data_num, [] (const RawData & l, const RawData & r) {
            return l.keyword < r.keyword;
        });
        end = std::chrono::high_resolution_clock::now();
        auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "排序数据所花费的时间：" << duration2 << " 毫秒" << std::endl;

        // // 打印数据
        // for(int i = 0; i < data_num; i++) {
        //     std::cout << raw_datas[i] << std::endl;
        // }
        
        // 将服务地址注册到etcd中
        // std::string str = "|" + std::to_string(data_num) + "|" + std::to_string(duration1) + "|" + std::to_string(duration2);
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

private:
    int load_csv_data() {// alloc memory
        raw_datas = new RawData[total_data_num];

        std::ifstream csv_file(filename);
        char buf[65536];
        csv_file.rdbuf()->pubsetbuf(buf, 65536);

        if (!csv_file.is_open()) {
            std::cout << "fiald to open " << filename << std::endl;
            return -1;
        }

        std::string line;
        while (std::getline(csv_file, line)) {
            RawData & rawData = raw_datas[data_num];
            if(!parserRawData(options_, line, rawData))
                continue;
            data_num++;
        }
        // 关闭文件
        csv_file.close();
        return 0;
    }
private:
    using MatchResult = std::pair<int64_t, int64_t>;
    std::unordered_map<uint64_t, MatchResult> search_caches;

public:
    std::pair<int64_t, int64_t> SearchByKeyWord(uint64_t keyword) {
        // cache
        if(search_caches.find(keyword) != search_caches.end()) {
            return search_caches[keyword];
        }
        auto findFirstLess = [this] (uint64_t keyword) -> int64_t{
            int64_t low = 0;
            int64_t high = data_num;
            while(low < high) {
                int64_t mid = ((high - low) >> 1) + low;
                if(raw_datas[mid].keyword < keyword) {
                    low = mid + 1;
                }else if(raw_datas[mid].keyword >= keyword) {
                    high = mid;
                }
            }
            return low;
        };
        MatchResult result;
        result.first = findFirstLess(keyword);
        if(raw_datas[result.first].keyword != keyword)
            return {-1 , -1};
        result.second = findFirstLess(keyword + 1) - 1;
        // add cache
        search_caches[keyword] = result;
        return result;
    }

    Status Search(ServerContext *context, const Request *request,
                    Response *response) override {
        uint64_t hour = request->hour();
        float vec1 = request->context_vector(0);
        float vec2 = request->context_vector(1);
        // std::cout << "vec1: " << vec1 << "  vec2: " << vec2 << "\n"; 
        float dist = sqrt(vec1 * vec1 + vec2 * vec2);
        int topn = request->topn();

        // 1. search match result
        // 预估点击率 = 商品向量 和 用户_关键词向量 的余弦距离
        // 排序分数 = 预估点击率 x 出价（分数越高，排序越靠前）
        std::unordered_map<uint64_t, SearchResult> search_map;
        for(int i = 0; i < request->keywords_size(); i++) {
            // search
            auto keyword = request->keywords(i);
            auto searchResult = SearchByKeyWord(keyword);
            if(searchResult.first == -1)
                continue;

            // std::cout << searchResult.first << "," << searchResult.second << std::endl;

            // 计算预估点击率 和分数
            for(int64_t i = searchResult.first; i <= searchResult.second; i++ ) {
                RawData& data = raw_datas[i];
                if(!(data.timings_mask & (1 << hour))) 
                    continue;
                // std::cout << data << "\n";

                float up = data.vec1 * vec1 + data.vec2 * vec2;
                float down = sqrt(data.vec1 * data.vec1 + data.vec2 * data.vec2) * dist;
                float ert = up / down + 0.000001f;
                float score = ert * data.price;
                // std::cout << "ert: " << ert << "-" << "score: " << score << "\n";

                if(search_map.find(data.adgroup_id) != search_map.end()) {
                    auto &result = search_map[data.adgroup_id];
                    if(score > result.score) {
                        result.price = data.price;
                        result.score = score;
                        result.ert = ert;
                    }else if(score == result.score) {
                        if(data.price < result.price) {
                            result.price = data.price;
                            result.ert = ert;
                        }
                    }
                }else{
                    SearchResult result(data.adgroup_id, data.price);
                    result.ert = ert;
                    result.score = score;
                    search_map[data.adgroup_id] = result;
                }
            }
        }

        if(search_map.empty())
            return Status::OK;

        std::vector<SearchResult> results;
        for(auto & it: search_map) {
            results.push_back(it.second);
        }

        // 3. top_k
        std::sort(results.begin(), results.end(), [] (SearchResult &lhs, SearchResult rhs) {
            if(lhs.score > rhs.score) {
                return true;
            }else if(lhs.score == rhs.score) {
                return lhs.price < rhs.price;
            }
            return false;
        });

        // 4. prices
        // 计费价格（计费价格 = 第 i+1 名的排序分数 / 第 i 名的预估点击率（i表示排序名次，例如i=1代表排名第1的广告））
        int n = results.size() <= topn ? results.size() : topn;
        for(int i = 0; i < n - 1; i++)
            results[i].bill_price = std::round(results[i+1].score / results[i].ert) ;
        results[n-1].bill_price = results.size() <= topn ? 
            results[n-1].price : std::round(results[n].score / results[n-1].ert);

        // 5. fill result
        n = results.size() <= topn ? results.size() : topn;
        for(int i = 0; i < n; i++) {
            response->add_adgroup_ids(results[i].adgroup_id);
            response->add_prices(results[i].bill_price);
            // std::cout << results[i].adgroup_id << "  " << results[i].bill_price << "\n";
        }
        return Status::OK;
    }

private:
    Options options_;
    std::string server_address_;
    etcd::Client etcd_;

    RawData* raw_datas;
    uint64_t data_num = 0;
};

void testService() {
    Options options = loadENV();
    std::string server_address = getLocalIP() + ":50051";
    std::cout << options << std::endl;

    SearchServiceImpl service(options, server_address);
    if(service.init() != 0) {
        std::cout << "SearchService faild to init ...." << std::endl;
        return ;
    }

    auto result = service.SearchByKeyWord(2916200016);
    std::cout << result.first << "," << result.second << std::endl;
    result = service.SearchByKeyWord(4803367238);
    std::cout << result.first << "," << result.second << std::endl;
    result = service.SearchByKeyWord(12210106372);
    std::cout << result.first << "," << result.second << std::endl;
    result = service.SearchByKeyWord(1);
    std::cout << result.first << "," << result.second << std::endl;
    result = service.SearchByKeyWord(2916200017);
    std::cout << result.first << "," << result.second << std::endl;
    result = service.SearchByKeyWord(12210106373);
    std::cout << result.first << "," << result.second << std::endl;
}

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
//   testService();
  return 0;
}
