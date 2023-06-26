#pragma once

#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "defines.h"

#include "test_case_reader.h"

class TestCaseReaderAsync : public TestCaseReader{
private:
    ConcurrentQueue<TestCasePair> testcase_;
    std::vector<std::thread> threads_;
    std::atomic<bool> enable_;
    std::atomic<uint64_t> read_num_;
    std::string filename_;
    std::shared_ptr<CSVDef> reader_;
    std::atomic<bool> reapeat_;

    void reloadfile() {
        std::shared_ptr<CSVDef> reader_ = std::make_shared<CSVDef>(this->filename_);
        // reader_->read_header(csv::ignore_missing_column, "keywords", "context_vector", "hour", "topn", "adgroup_ids", "prices");
        this->reader_.swap(reader_);
    }

public:
    TestCaseReaderAsync(std::string filename_, int32_t capacity): 
        threads_(1),testcase_(capacity, "testcase_reader"),enable_{true},read_num_{0},reapeat_{false} {
        this->filename_ = filename_;
    }
    TestCaseReaderAsync(const TestCaseReaderAsync&) = delete;
    TestCaseReaderAsync(TestCaseReaderAsync&&) = delete;
    TestCaseReaderAsync& operator=(const TestCaseReaderAsync&) = delete;
    TestCaseReaderAsync& operator=(TestCaseReaderAsync&&) = delete;
    ~TestCaseReaderAsync() override {
    }
    void Start() override {
        this->reloadfile();

        for (size_t i = 0; i < this->threads_.size(); ++i) {
            threads_[i] = std::thread([&]() {
                while(true) {
                    RequestPtr request = std::make_shared<alimama::proto::Request>();
                    ResponsePtr response = std::make_shared<alimama::proto::Response>();
                    this->ReadNextRow(request, response);
                    this->read_num_ ++;
                    BOOST_LOG_TRIVIAL(trace) << "reapeat_ " << this->reapeat_ << " read_num_: " << this->read_num_;

                    TestCasePair pair_data{ this->reapeat_, request, response };
                    if (this->testcase_.Push(pair_data) == QueueStatus::Closed) {
                        BOOST_LOG_TRIVIAL(trace) << "testcase_ reader_ closed";
                        break;
                    }
                }
            });
        }
    }

    void ReadNextRow(RequestPtr& request, ResponsePtr& response) {
        std::string keywords, context_vector;
        uint64_t hour, topn;
        std::string adgroup_ids, prices;
        auto ok = this->reader_->read_row(keywords, context_vector, hour, topn, adgroup_ids, prices);
        if (!ok) {
            this->reapeat_ = true;
            this->reloadfile();
            this->reader_->read_row(keywords, context_vector, hour, topn, adgroup_ids, prices);
        }

        for (auto keyword : ParseUint64List(keywords)) {
            request->add_keywords(keyword);
        }
        for (auto vec : ParseFloatList(context_vector)) {
            request->add_context_vector(vec);
        }
        request->set_hour(hour);
        request->set_topn(topn);
        for (auto id : ParseUint64List(adgroup_ids)) {
            response->add_adgroup_ids(id);
        }
        for (auto price : ParseUint64List(prices)) {
            response->add_prices(price);
        }
    }

    void Stop() override {
        this->enable_.store(false);
        this->testcase_.Close();
        for (size_t i = 0; i < this->threads_.size(); ++i) {
            this->threads_[i].join();
        }
    }

    bool Pop(TestCasePair& pair) override {
        if (this->testcase_.WaitAndPop(pair) == QueueStatus::Closed) {
            return false;
        }
        return true;
    }
};
