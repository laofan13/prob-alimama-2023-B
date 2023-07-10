#include <cstdio>
#include <iostream>
#include <fstream>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>

const std::string filename = "../../data/data.csv";

struct Options {
    int node_id;
    int node_num;
    int cpu;
    uint64_t mempry;
};

std::ostream& operator<<(std::ostream& os, Options& options) {
    os << "node Option information: {"  <<  std::endl;
    os << '\t' << "node_id: " << options.node_id << std::endl
        << '\t' << "node_num: " << options.node_num << std::endl
        << '\t' << "mempry: " << options.mempry << std::endl
        << '\t' << "cpu: " << options.cpu << std::endl;
    os << '\t' << "}"<< std::endl;
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

// struct RawData {
//     uint64_t keywrod;
//     uint64_t adgroup_id;
//     uint16_t price;
//     uint16_t status;
//     int32_t timings_mask;
//     float vec1, vec2;
//     uint64_t campaign_id;
//     uint64_t item_id;
// };

struct RawData {
    uint64_t adgroup_id;
    uint16_t price;
    int32_t timings;
    float vec1, vec2;
};

std::ostream& operator<<(std::ostream& os, RawData& raw_data) {
    os << "RawData information: {"  <<  "\n";
    os << '\t' << "adgroup_id: " << raw_data.adgroup_id << "\n"
        << '\t' << "price: " << raw_data.price << "\n"
        << '\t' << "timings: " << std::hex << raw_data.timings << std::dec <<std::endl
        << '\t' << "vector: ["<< raw_data.vec1 << ", "<< raw_data.vec2 <<"]" << "\n";
    os << "}"<< std::endl;
    return os;
}

std::unordered_map<int64_t, std::vector<RawData>> load_data(const std::string& filename) {
    std::ifstream csv_file(filename);

    if (!csv_file.is_open()) {
        std::cout << "fiald to open " << filename << std::endl;
        return {};
    }

    std::unordered_map<int64_t, std::vector<RawData>> hash_tables(1e7);
    
    int64_t key_word = 0;
    int status = 0; 
    std::string line, cell;
    while (std::getline(csv_file, line)) {
        std::istringstream iss(line);

        RawData rawData;

        // keywrod
        iss >> cell;
        key_word = std::stoull(cell);
        // std::cout << rawData.keywrod << " | ";

        // adgroup_id
        iss >> cell;
        rawData.adgroup_id = std::stoull(cell);
        // std::cout << rawData.adgroup_id << " | ";

        // price
        iss >> cell;
        rawData.price = std::stoul(cell);
        // std::cout << rawData.price << " | ";
        // status
        iss >> cell;
        status = std::stoi(cell);
        if(status == 0)
            continue;
        // std::cout << rawData.status << " | ";

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
        // std::cout << rawData.timings_mask << " | ";
        // printf("%x | ", rawData.timings_mask);
        // vector
        iss >> cell;
        int pos = cell.find_first_of(',');
        rawData.vec1 = std::stof(cell.substr(0, pos));
        rawData.vec2 = std::stof(cell.substr(pos + 1));
        // std::cout << rawData.vec1 << " | ";
        // std::cout << rawData.vec2 << " | ";
        // campaign_id
        // iss >> cell;
        // rawData.campaign_id = std::stoull(cell);
        // std::cout << rawData.campaign_id << " | ";
        // item_id
        // iss >> cell;
        // rawData.item_id = std::stoull(cell);
        // std::cout << rawData.item_id ;

        if(hash_tables.find(key_word) == hash_tables.end()) {
            hash_tables[key_word] = {};
        }
        hash_tables[key_word].push_back(rawData);
    }
    return std::move(hash_tables);

}

int main(int argc, char **argv) { 
    auto hash_tables = load_data(filename);
    for(auto & pair: hash_tables) {
        std::cout << "keyword:" << pair.first << std::endl;
        for(auto & data: pair.second) {
            std::cout << data;
        }
    }
    // hash_tables

    return 0;

}
