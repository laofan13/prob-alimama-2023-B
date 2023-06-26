#pragma once

#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "defines.h"

#include "test_case_reader.h"

class TestCaseReaderPreload : public TestCaseReader{
private:
    std::vector<TestCasePair> testcase_;
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
    TestCaseReaderPreload(std::string filename_, int32_t capacity): 
        testcase_{},read_num_{0},reapeat_{false} {
        this->filename_ = filename_;
    }
    TestCaseReaderPreload(const TestCaseReaderPreload&) = delete;
    TestCaseReaderPreload(TestCaseReaderPreload&&) = delete;
    TestCaseReaderPreload& operator=(const TestCaseReaderPreload&) = delete;
    TestCaseReaderPreload& operator=(TestCaseReaderPreload&&) = delete;
    virtual ~TestCaseReaderPreload() {
    }
    void Start() override {
        this->reloadfile();
        while(true) {
            RequestPtr request = std::make_shared<alimama::proto::Request>();
            ResponsePtr response = std::make_shared<alimama::proto::Response>();
            auto ok =this->ReadNextRow(request, response);
            if (!ok) {
                break;
            }
            this->testcase_.push_back(TestCasePair{
                false, request, response
            });
        }
    }

    bool ReadNextRow(RequestPtr& request, ResponsePtr& response) {
        std::string keywords, context_vector;
        uint64_t hour, topn;
        std::string adgroup_ids, prices;
        auto ok = this->reader_->read_row(keywords, context_vector, hour, topn, adgroup_ids, prices);
        if (!ok) {
            return false;
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
        return true;
    }

    void Stop() override {
    }

    bool Pop(TestCasePair& pair) override {
        BOOST_LOG_TRIVIAL(trace) << "read_num_ " << this->read_num_ << " this->testcase_.size(): " << this->testcase_.size();
        if (this->read_num_ >= this->testcase_.size()) {
            this->read_num_ = 0;
            this->reapeat_ = true;
        }
        pair = this->testcase_[this->read_num_];
        pair.repeat = this->reapeat_;

        this->read_num_ ++;
        return true;
    }
};