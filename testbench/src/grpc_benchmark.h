#pragma once

#include <boost/heap/priority_queue.hpp>
#include <boost/lockfree/queue.hpp>
#include <thread>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <unistd.h>
#include <time.h>

#include <grpcpp/impl/codegen/status.h>
#include <grpcpp/impl/codegen/client_context.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "concurent_queue.h"
#include "token_bucket.h"

#include "log.h"

using std::shared_ptr;
using grpc::Status;
using grpc::ClientContext;
using grpc::Channel;

void sleepNanoseconds(unsigned long long nanoseconds) {
    struct timespec sleepTime;
    sleepTime.tv_sec = nanoseconds / 1000000000;
    sleepTime.tv_nsec = nanoseconds % 1000000000;
    nanosleep(&sleepTime, NULL);
}

template <typename T_Req, typename T_Resp>
class GrpcClient {
public:
    virtual ~GrpcClient() {}
    virtual bool Init() = 0;
    virtual bool Request(shared_ptr<ClientContext> ctx, T_Req& req, void* obj) = 0;
    virtual bool WaitResponse(T_Resp& resp, shared_ptr<Status>& status, void** obj) = 0;
    virtual bool Close() = 0;
};

template <typename T_Req, typename T_Resp, typename T_Cusom_Summary, typename T_Ref>
class GrpcBenchmark {
public:
    class Comparator {
    public:
        virtual ~Comparator() {}
        virtual bool Compare(const T_Resp& resp, const T_Ref& ref, T_Cusom_Summary& result)  = 0;
    };
    struct SummaryData {
        uint64_t completed_requests;
        uint64_t success_request_count;
        double  success_request_percent;
        uint64_t error_request_count;
        uint64_t timeout_request_count;
        double avg_latency_ms;
        double p99_latency_ms;
        bool interupted;
        T_Cusom_Summary custom_summary;
    };
    struct EachResp {
        uint64_t id;
        std::chrono::steady_clock::time_point start_t;
        T_Resp response;
        T_Ref ref_resp;
        shared_ptr<Status> status;
        double latency;
    };
    struct EachReqInternal {
        uint64_t id;
        T_Req request;
        T_Ref ref_resp;
    };
    struct GrpcClientCollection {
        shared_ptr<GrpcClient<T_Req, T_Resp>> cli;
        ConcurrentQueue<EachReqInternal> requests;
        ConcurrentQueue<EachResp> responses;
    };
    using ComparatorPtr = std::shared_ptr<Comparator>;
private:
    ComparatorPtr comparator_;
    ConcurrentQueue<EachReqInternal> requests_;
    ConcurrentQueue<EachResp> responses_;

    std::vector<std::thread> threads_;
    std::vector<shared_ptr<GrpcClientCollection>> cli_collection_;
    
    std::vector<std::thread> collectors_;
    std::vector<std::thread> calculators_;

    std::atomic<bool> enable_;
    std::atomic<int32_t> pending_num_;
    std::mutex mutex_;
    std::condition_variable cv_wait_;

    std::atomic<int32_t> pending_compare_num_;
    std::mutex summary_mutex_;
    std::condition_variable summary_cv_wait_;

    SummaryData summary_data_;
    double total_latency_;
    ConcurrentQueue<double> latencies_;

    int64_t deadline_ms_;
    std::atomic<uint64_t> req_idx_;

    bool updateSummary(const EachResp& resp) {
        this->summary_data_.completed_requests ++;
        if (!resp.status->ok() && resp.status->error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            this->summary_data_.timeout_request_count ++;
            return false;
        } else if (!resp.status->ok()) {
            this->summary_data_.error_request_count ++;
            return false;
        } else {
            this->total_latency_ += resp.latency;
            this->summary_data_.success_request_count ++;
            this->latencies_.Push(resp.latency);
            return true;
        }
        return true;
    }

    void processRequestWorker(shared_ptr<GrpcClientCollection> collection, int32_t qps_each_thread) {
        EachReqInternal each;
        TokenBucket tb(qps_each_thread, 1);
        while (this->enable_.load()) {
            auto status = collection->requests.WaitAndPop(each);
            if(status == QueueStatus::Closed) break;
            
            auto ctx = std::make_shared<ClientContext>();
            gpr_timespec timeout = gpr_time_from_millis(this->deadline_ms_, GPR_TIMESPAN);
            ctx->set_deadline(timeout);

            auto* eachResp = new EachResp{};
            eachResp->id = each.id;
            eachResp->ref_resp = each.ref_resp;
            tb.Consume(1);
            eachResp->start_t = std::chrono::steady_clock::now();
            bool ok = collection->cli->Request(ctx, each.request, eachResp);
            if (!ok) {
                BOOST_LOG_TRIVIAL(warning) << "request failed ";
                continue;
            }
        }
    }
    void collectResponseWorker(shared_ptr<GrpcClientCollection> collection) {
        T_Resp resp{};
        shared_ptr<Status> status{};
        void* obj;
        while(collection->cli->WaitResponse(resp, status, &obj)) {
            EachResp* eachResp = (EachResp*) obj;
            auto end = std::chrono::steady_clock::now();
            auto latency = std::chrono::duration<double, std::milli>(end - eachResp->start_t).count();
            EachResp respout{
                eachResp->id,
                eachResp->start_t,
                resp,
                eachResp->ref_resp,
                status,
                latency
            };
            this->pending_num_ --;
            if (this->pending_num_ <= 0) {
                this->cv_wait_.notify_all();
            }
            this->pending_compare_num_ ++;
            auto pushstat = this->responses_.Push(respout);
            delete eachResp;
        }
    }
    void calculateStatsWorker() {
        EachResp resp{};
        int processed{};
        while(this->responses_.WaitAndPop(resp) != QueueStatus::Closed) {
            processed++;
            if (processed % 500 == 0) {
                BOOST_LOG_TRIVIAL(trace) << "pending_compare_num_  " << this->pending_compare_num_.load() ;
            }
            bool ok{true};
            if (this->updateSummary(resp)) {
                ok = comparator_->Compare(resp.response, resp.ref_resp, this->summary_data_.custom_summary);
            }
            this->pending_compare_num_ --;
            if (this->pending_compare_num_ <= 0) {
                this->summary_cv_wait_.notify_all();
            }
            if (!ok) {
                this->summary_data_.interupted = true;
                this->enable_.store(false);
            }
        }
    }

public:
    GrpcBenchmark(std::vector<shared_ptr<GrpcClient<T_Req, T_Resp>>> clis, ComparatorPtr comparator,
            int64_t deadline_ms_=10, int32_t qps_limit=1000, int32_t calculator_num=1):
            comparator_{comparator}, enable_(true), pending_num_{0}, pending_compare_num_{0},
            deadline_ms_{deadline_ms_},requests_(0, "bencharmk_requests"),responses_(0, "bencharmk_responses"),
            summary_data_{},total_latency_{},req_idx_{0},latencies_{0} {
        int32_t threads_num = clis.size();
        threads_.resize(threads_num);
        cli_collection_.resize(threads_num);
        collectors_.resize(threads_num);
        calculators_.resize(calculator_num);

        auto qps_each_thread = qps_limit / threads_num;
        BOOST_LOG_TRIVIAL(info) << " threads_num " << threads_num << " deadline_ms_ " << deadline_ms_ << " qps_limit " << qps_limit << " qps_each_thread " << qps_each_thread;

        if (clis.size() != threads_num) {
            BOOST_LOG_TRIVIAL(error) << " invalid clis num " << threads_num << " clis.size() " << clis.size();
            return;
        }
        for(size_t i=0; i<threads_num; i++) {
            auto collection = std::make_shared<GrpcClientCollection>();
            collection->cli = clis[i];
            cli_collection_[i].swap(collection);
        }
        for (size_t i = 0; i < this->threads_.size(); ++i) {
            threads_[i] = std::thread([this, i, qps_each_thread]() {
                this->processRequestWorker(this->cli_collection_[i], qps_each_thread);
            });
        }
        for(size_t i=0; i<cli_collection_.size(); i++) {
            this->collectors_[i] = std::thread([this, i]() {
                this->collectResponseWorker(this->cli_collection_[i]);
            });
        }
        for(size_t i=0; i<calculators_.size(); i++) {
            this->calculators_[i] = std::thread([this]() {
                this->calculateStatsWorker();
            });
        }
    }
    GrpcBenchmark(const GrpcBenchmark&) = delete;
    GrpcBenchmark(GrpcBenchmark&&) = delete;
    GrpcBenchmark& operator=(const GrpcBenchmark&) = delete;
    GrpcBenchmark& operator=(GrpcBenchmark&&) = delete;

    ~GrpcBenchmark() {
        this->enable_.store(false);
        for (auto& collection : cli_collection_) {
            collection->requests.Close();
            collection->responses.Close();
            collection->cli->Close();
        }
        this->cv_wait_.notify_all();
        this->summary_cv_wait_.notify_all();
        for (auto& thread : threads_) {
            thread.join();
        }
        for (auto& calculator : calculators_) {
            calculator.join();
        }
        for (auto& collector : collectors_) {
            collector.join();
        }
    }

    bool Request(uint64_t id, const T_Req& req, const T_Ref& ref_resp) {
        this->pending_num_ ++;
        EachReqInternal internal {
            id,
            req,
            ref_resp
        };
        auto idx = req_idx_.fetch_add(1);
        idx = idx % this->cli_collection_.size();
        if (this->cli_collection_[idx]->requests.Push(internal) == QueueStatus::Closed) {
            return false;
        } else {
            return true;
        }
    }

    void WaitAll() {
        while (this->pending_num_ > 0 && this->enable_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        this->enable_.store(false);
        this->requests_.Close();
        this->pending_num_.store(0);
    }

    // wait all request
    bool WaitAllUntilTimeout(int64_t timeout_ms=0) {
        std::unique_lock<std::mutex> lock(this->mutex_);

        bool not_time_out{false};
        if (this->cv_wait_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
                return !this->enable_.load() || (this->enable_.load() && this->pending_num_.load() == 0); })) {
            not_time_out = true;
        }
        this->enable_.store(false);
        this->requests_.Close();
        this->pending_num_.store(0);
        return not_time_out;
    }

    SummaryData Summary() {
        std::unique_lock<std::mutex> lock(this->summary_mutex_);
        this->summary_cv_wait_.wait(lock, [this] {
            BOOST_LOG_TRIVIAL(warning) << "this->pending_num_ " << this->pending_num_ << " this->pending_compare_num_ " << this->pending_compare_num_;
            return this->pending_num_ <= 0 && this->pending_compare_num_ <= 0;
        });
        this->responses_.Close();

        this->summary_data_.avg_latency_ms = this->total_latency_ / this->summary_data_.success_request_count;
        int32_t p99_index = int32_t(this->summary_data_.success_request_count * 0.01);

        boost::heap::priority_queue<double> pq{};
        double latency{};
        while(this->latencies_.TryPop(latency) != QueueStatus::Empty) {
            pq.push(latency);
        }
        boost::heap::priority_queue<double>::const_iterator it = pq.begin(); 
        std::advance(it, p99_index);
        if (it != pq.end()) {
            this->summary_data_.p99_latency_ms = *it;
        }
        this->summary_data_.success_request_percent = this->summary_data_.success_request_count * 1.0 / this->summary_data_.completed_requests;
        return this->summary_data_;
    }
};
