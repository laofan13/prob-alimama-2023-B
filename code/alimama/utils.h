#pragma once
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <list>
#include <condition_variable>

#include <iomanip>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>


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

//byte size = 8 + 8 + 8 + 8
#pragma pack(8)
struct RawData {
    uint64_t keyword;
    uint64_t adgroup_id;
    uint16_t price;
    uint32_t timings_mask;
    float item_vec1, item_vec2;
    // uint64_t campaign_id;
    // uint64_t item_id;
};

std::ostream& operator<<(std::ostream& os, RawData& data) {
    os << "keyword: " << data.keyword << " | "
        << "adgroup_id: " << data.adgroup_id << " | "
        << "price: " << data.price << " | "
        << "timings_mask: " << std::hex << data.timings_mask << std::dec << " | "
        << "vector: ["<< data.item_vec1 << ", "<< data.item_vec2 << "]";
    return os;
}

bool parserRawData(Options option, std::string & line, RawData &data) {
    int i = 0;
    int n = line.size();
    // key_word
    data.keyword = 0;
    while(i < n && line[i] != '\t') {
        data.keyword = data.keyword * 10 + (line[i] - '0');
        i++;
    }
    while(i < n && line[i] == '\t')
        i++; 

    if(data.keyword % option.node_num != option.node_id)
        return false;

    // adgroup_id
    data.adgroup_id = 0;
    while(i < n && line[i] != '\t') {
        data.adgroup_id = data.adgroup_id * 10 + (line[i] - '0');
        i++;
    }
    while(i < n && line[i] == '\t')
        i++;

    // price
    data.price = 0;
    while(i < n && line[i] != '\t') {
        data.price = data.price * 10 + (line[i] - '0');
        i++;
    }
    while(i < n && line[i] == '\t')
        i++;

    // status
    if(line[i] == '0')
        return false;
    i++;
    while(i < n && line[i] == '\t')
        i++;

    // timings
    data.timings_mask = 0;
    int k = 0;
    while(i < n) {
        if(line[i] == '0') {
            k++;
        }else if(line[i] == '1') {
            data.timings_mask |= 1 << k;
            k++;
        }
        i++;
        if(k >= 24) 
            break;
    }
    while(i < n && line[i] == '\t')
        i++;

    // vector1
    data.item_vec1 = 0.0f;
    float decimal = 0.1f;
    while (i < n && line[i] != '.') {
        data.item_vec1 = data.item_vec1 * 10.0f + (line[i] - '0');
        ++i;
    }
    ++i;
    while (i < n && line[i] != ',') {
        data.item_vec1 = data.item_vec1 + (line[i] - '0') * decimal;
        decimal *= 0.1f;
        ++i;
    }
    i++;

    // vector2
    data.item_vec2 = 0.0f;
    decimal = 0.1f;
    while (i < n && line[i] != '.') {
        data.item_vec2 = data.item_vec2 * 10.0f + (line[i] - '0');
        ++i;
    }
    ++i;
    while (i < n && line[i] != '\t') {
        data.item_vec2 = data.item_vec2 + (line[i] - '0') * decimal;
        decimal *= 0.1f;
        ++i;
    }
    return true;
};

#pragma pack(8)
struct SearchResult {
    uint64_t adgroup_id;
    uint16_t price;
    float ctr;
    float score;
    uint64_t bill_price = 0;

    SearchResult() {}
    SearchResult(uint64_t id, uint16_t p): adgroup_id(id), price(p) {}
    SearchResult(uint64_t id, uint16_t p, float e, float s): 
        adgroup_id(id), price(p), ctr(e), score(s)  {}
};

std::ostream& operator<<(std::ostream& os, SearchResult& result) {
    os << "adgroup_id: " << result.adgroup_id << " | "
        << "price: " << result.price << " | "
        << "ctr: " << std::setprecision(6)<< result.ctr << " | "
        << "score: " << std::setprecision(15)<< result.score << " | "
        << "bill_price: " << result.bill_price;
    return os;
}


bool order_cmp(const SearchResult &lhs, const SearchResult rhs) {
    if(std::abs(lhs.score - rhs.score) <= epsilon) {
        return lhs.price == rhs.price ? lhs.adgroup_id > rhs.adgroup_id : lhs.price < rhs.price;
    }
    return lhs.score > rhs.score;
}

// struct SearchTask{
//     std::vector<uint64_t> keywords;
//     float context_vector1;
//     float context_vector2;
//     uint64_t hour;
//     uint64_t topn;

//     SearchTask(float v1, float v2, uint64_t h, uint64_t n):
//         context_vector1(v1),
//         context_vector2(v2),
//         hour(h),
//         topn(n)
//     {

//     }
// };

struct AyncSearchResult {
    bool finish;
    bool faild;
    std::mutex mu;
    std::condition_variable cv;

    std::vector<SearchResult> results;

    AyncSearchResult():
         finish(false),
         faild(false),
         results(0)
    {

    }

    void wait() {
        while(1) {
            if( finish || faild)
                break;
            std::unique_lock<std::mutex> lock(mu);
            cv.wait(lock, [this] { 
                return finish || faild; 
            });
            if( finish || faild)
                break;
        }
    };

    void Finish() {
        {
            std::unique_lock<std::mutex> lock(mu);
            finish = true;
        }
        cv.notify_one();
    }

    void Cancel() {
        {
            std::unique_lock<std::mutex> lock(mu);
            faild = true;
        }
        cv.notify_one();
    }
};