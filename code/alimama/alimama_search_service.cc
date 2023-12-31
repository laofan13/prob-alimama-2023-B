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
#include "search_service_client.h"

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
        // 加载广告数据
        auto start = std::chrono::high_resolution_clock::now();
        if(load_csv_data() != 0) {
            std::cout << "fiald to load data " << filename << "\n";
            return -1;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto load_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        std::cout << "加载文件所花费的时间：" << load_duration << " 毫秒" << "\n";
        std::cout << "文件行数：" << data_num << "\n";

        // 排序
        start = std::chrono::high_resolution_clock::now();
        std::sort(raw_datas, raw_datas + data_num, [] (const RawData & l, const RawData & r) {
            return l.keyword < r.keyword;
        });
        end = std::chrono::high_resolution_clock::now();
        auto sort_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        std::cout << "对数据进行排序所花费的时间：" << sort_duration << " 毫秒" << "\n";

        // for(int64_t i = 0; i < data_num; i++)
        //     std::cout << raw_datas[i] << "\n";
            
        // 构建索引
        start = std::chrono::high_resolution_clock::now();
        indexs.reserve(key_word_num);
        int64_t s = 0;
        for(int64_t i = 1; i < data_num; i++) {
            if(raw_datas[i].keyword != raw_datas[i-1].keyword) {
                indexs[raw_datas[s].keyword] = {s, i-1};
                s = i;
            }
        }
        indexs[raw_datas[s].keyword] = {s, data_num - 1};
        end = std::chrono::high_resolution_clock::now();
        auto index_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        std::cout << "构建索引所花费的时间：" << index_duration << " 毫秒" << "\n";

        // for(auto & it: indexs) {
        //     std::cout << it.first << " = [" << it.second.first << "," <<  it.second.second << "]"
        //         << "\n";
        // }
        
        // 将服务地址注册到etcd中
        if(!registerOrFoundService()) {
            std::cout << "faild to register Or Found Service ...." << "\n";
            return -1;
        }
        return 0;
    }

    int load_csv_data() {
        // alloc memory
        raw_datas = new RawData[total_data_num];

        std::ifstream csv_file(filename);
        char buf[65536];
        csv_file.rdbuf()->pubsetbuf(buf, 65536);

        if (!csv_file.is_open()) {
            std::cout << "fiald to open " << filename << "\n";
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
    
    // node 0 对外服务， node1 对内服务
    bool registerOrFoundService() {
        if(options_.node_id == 0) {
            //wait node1 
            std::chrono::seconds retry_delay(1);
            std::string node1_address = "";
            std::cout << "node_"<< options_.node_id << " try found other service" << "\n";
            while(1) {
                etcd::Response resp = etcd_.get(modelservice_node1).get();
                if (resp.is_ok()) {
                    node1_address = resp.value().as_string();
                    break;
                }
                std::this_thread::sleep_for(retry_delay);
            }
            std::cout << "try connect grpc service, node1: "
                    << " server_address" << node1_address << "\n";
            std::shared_ptr<Channel> channel = grpc::CreateChannel(
                node1_address, grpc::InsecureChannelCredentials());
            gpr_timespec tm_out{ 3, 0, GPR_TIMESPAN };
            if(!channel->WaitForConnected(gpr_time_add(
                    gpr_now(GPR_CLOCK_REALTIME),
                    gpr_time_from_seconds(60, GPR_TIMESPAN)))) {
                std::cout << "faild to connect grpc server: " << node1_address << std::endl;
                return false;
            }
            ayncSearchClient = std::make_shared<SearchServiceClient>(options_, channel);
            ayncSearchClient->init();

            auto response = etcd_.set(modelservice_key, server_address_).get();
            if (response.is_ok()) {
                std::cout << "Service registration successful.\n";
            } else {
                std::cerr << "Service registration failed: " << response.error_message()
                        << "\n";
                return false;
            }
        }else if(options_.node_id == 1){
            auto response = etcd_.set(modelservice_node1, server_address_).get();
            if (response.is_ok()) {
                std::cout << "Service registration successful.\n";
            } else {
                std::cerr << "Service registration failed: " << response.error_message()
                        << "\n";
                return -1;
            }
        }
        return true;
    }

    std::vector<SearchResult> searchTopNByKeyword(const Request *request) {
        uint64_t hour = request->hour();
        float context_vec1 = request->context_vector(0);
        float context_vec2 = request->context_vector(1);
        float context_dist = sqrt(context_vec1 * context_vec1 + context_vec2 * context_vec2);
        int topn = request->topn();

        // 1. search match result
        std::vector<SearchResult> search_results;
        std::unordered_map<uint64_t, int> deduplication_maps;
        for(int i = 0; i < request->keywords_size(); i++) {
            // Recall
            auto keyword = request->keywords(i);
            // std::cout << "keyword=" << keyword << std::endl;
            if(keyword % options_.node_num != options_.node_id)
                continue;
            if(indexs.find(keyword) == indexs.end()) 
                continue;
            auto matchResult = indexs[keyword];
            

            // 计算预估点击率 和分数
            for(int64_t i = matchResult.first; i <= matchResult.second; i++ ) {
                RawData& data = raw_datas[i];
                // Filtering
                if(!(data.timings_mask & (1 << hour))) 
                    continue;

                // 预估点击率 = 商品向量 和 用户_关键词向量 的余弦距离
                // 排序分数 = 预估点击率 x 出价（分数越高，排序越靠前）
                float up = data.item_vec1 * context_vec1 + data.item_vec2 * context_vec2;
                float down = sqrt(data.item_vec1 * data.item_vec1 + data.item_vec2 * data.item_vec2) * context_dist;
                float ctr = up / down + 0.000001f;
                float score = ctr * data.price;

                // 去重，以确保同一个广告不会被多次展示给用户。
                // 排序过程遇到adgroup_id重复，则优先选择排序分数高者，如若同时排序分数相等则取出价低者；
                if(deduplication_maps.find(data.adgroup_id) != deduplication_maps.end()) {
                    auto &result = search_results[deduplication_maps[data.adgroup_id]];
                    if(score > result.score || (score == result.score && data.price < result.price)) {
                        result.price = data.price;
                        result.score = score;
                        result.ctr = ctr;
                    }
                }else{
                    search_results.emplace_back(data.adgroup_id, data.price, ctr, score);
                    deduplication_maps[data.adgroup_id] = search_results.size() - 1;
                }   
            }
        }
        if(search_results.empty())
            return {};

        // 2.分数越高，排序越靠前
        std::sort(search_results.begin(), search_results.end(), [] (SearchResult &lhs, SearchResult rhs) {
            if(lhs.score > rhs.score) {
                return true;
            }else if(lhs.score == rhs.score) {
                if(lhs.price < rhs.price) {
                    return true;
                }else if(lhs.price == rhs.price) {
                    return lhs.adgroup_id > rhs.adgroup_id;
                }
                return false;
            }
            return false;
        });
        // 3. 保留topN
        if(search_results.size() > topn) 
            search_results.resize(topn+1);

        return std::move(search_results);
    }

    Status InernalSearch(ServerContext *context, const Request *request, SearchResponse *response) override {
        auto search_results = searchTopNByKeyword(request);
        int n = search_results.size();
        response->mutable_adgroup_ids()->Reserve(n);
        response->mutable_prices()->Reserve(n);
        response->mutable_ctrs()->Reserve(n);
        response->mutable_scores()->Reserve(n);
        for(auto &data: search_results) {
            response->add_adgroup_ids(data.adgroup_id);
            response->add_prices(data.price);
            response->add_ctrs(data.ctr);
            response->add_scores(data.score);
        }
        return Status::OK;
    }

    std::vector<SearchResult> mergeResult(std::vector<SearchResult> & result1, std::vector<SearchResult> & result2) {
        auto sort_cmp = [] (const SearchResult &lhs, const SearchResult rhs) {
            if(lhs.score > rhs.score) {
                return true;
            }else if(lhs.score == rhs.score) {
                if(lhs.price < rhs.price) {
                    return true;
                }else if(lhs.price == rhs.price) {
                    return lhs.adgroup_id > rhs.adgroup_id;
                }
                return false;
            }
            return false;
        };
        std::vector<SearchResult> search_results;
        std::unordered_map<uint64_t, int> deduplication_maps;

        // 去重，以确保同一个广告不会被多次展示给用户。
        // 排序过程遇到adgroup_id重复，则优先选择排序分数高者，如若同时排序分数相等则取出价低者；
        auto insertResult = [&search_results, &deduplication_maps](SearchResult &data) {
            if(deduplication_maps.find(data.adgroup_id) != deduplication_maps.end()) {
                auto &result = search_results[deduplication_maps[data.adgroup_id]];
                if(data.score > result.score || (data.score == result.score && data.price < result.price)) {
                    result.price = data.price;
                    result.score = data.score;
                    result.ctr = data.ctr;
                }
            }else{
                search_results.emplace_back(data.adgroup_id, data.price, data.ctr, data.score);
                deduplication_maps[data.adgroup_id] = search_results.size() - 1;
            }
        };

        int i = 0, j = 0;
        while(i < result1.size() && j < result2.size()) {
            if(sort_cmp(result1[i], result2[j])) {
                insertResult(result1[i]);
                i++;
            }else{
                insertResult(result2[j]);
                j++;
            }
        }
        while(i < result1.size()) {
            insertResult(result1[i]);
            i++;
        }
        while(j < result2.size()) {
            insertResult(result2[j]);
            j++;
        }

        return std::move(search_results);
    }

    Status Search(ServerContext *context, const Request *request,
                    Response *response) override {
        
        uint64_t topn = request->topn();
        AyncSearchResult* searchResult = new AyncSearchResult();
        ayncSearchClient->Search(request, searchResult);
        // local search
        auto search_results = searchTopNByKeyword(request);
        // wait node1 relpy
        searchResult->wait();
        
        auto final_results = mergeResult(search_results, searchResult->results);
        // free AyncSearchResult
        {
            delete searchResult;
        }

        // 4. prices
        // 计费价格（计费价格 = 第 i+1 名的排序分数 / 第 i 名的预估点击率（i表示排序名次，例如i=1代表排名第1的广告））
        int n = final_results.size() <= topn ? final_results.size() : topn;
        for(int i = 0; i < n - 1; i++)
            final_results[i].bill_price = std::round(final_results[i+1].score / final_results[i].ctr) ;
        final_results[n-1].bill_price = final_results.size() <= topn ? 
            final_results[n-1].price : std::round(final_results[n].score / final_results[n-1].ctr);

        // 5. fill result
        n = final_results.size() <= topn ? final_results.size() : topn;
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
    
    //data
    RawData* raw_datas;
    int64_t data_num = 0;
    // index
    using IndexResult = std::pair<int64_t, int64_t>;
    std::unordered_map<uint64_t, IndexResult> indexs;

    std::shared_ptr<SearchServiceClient> ayncSearchClient;
};

void RunServer() {
    Options options = loadENV();
    if(options.node_id == 2) {
        std::cout << "node3 exit ..." << "\n";
        return;
    }
        
    options.node_num = 2;
    std::string server_address = getLocalIP() + ":50051";
    std::cout << options << std::endl;

    SearchServiceImpl service(options, server_address);
    if(service.init() != 0) {
        std::cout << "SearchService faild to init ...." << std::endl;
        return ;
    }
    options.node_num = 2;

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
