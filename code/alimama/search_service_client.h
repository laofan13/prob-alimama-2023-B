#pragma once

#include <ostream>
#include <string>

#include "config.h"
#include "utils.h"

#include "alimama.pb.h"
#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/alimama.grpc.pb.h"
#else
#include "alimama.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientAsyncResponseReader;
using grpc::CompletionQueue;

using alimama::proto::Request;
using alimama::proto::SearchResponse;
using alimama::proto::SearchService;

class SearchServiceClient {
public:
    SearchServiceClient(Options option, std::shared_ptr<Channel> channel_)
        :options(option),
         stub_(SearchService::NewStub(channel_))
    {

    }
    ~SearchServiceClient() {
        if(bg_thread) {
            if(bg_thread->joinable())
                bg_thread->join();
            delete bg_thread;
        }
    }
    

    void init() {
        bg_thread = new std::thread([this]  {
            void* got_tag;
            bool ok = false;
            // Block until the next result is available in the completion queue "cq".
            while (cq_.Next(&got_tag, &ok)) {
                // The tag in this example is the memory location of the call object
                AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

                // Verify that the request was completed successfully. Note that "ok"
                // corresponds solely to the request for updates introduced by Finish().
                GPR_ASSERT(ok);

                HandleResponse(&call->response, call->searchResult, call->status);

                // Once we're complete, deallocate the call object.
                delete call;
            }
        });
    }

    void Search(const Request *req, AyncSearchResult* searchResult) {
         // std::cout << "node_" << node_id_ << "push num: " << tasks.num << std::endl;
        Request new_request;
        for (int i = 0; i < req->keywords_size(); ++i) {
            if(req->keywords(i) % options.node_num == 1){
                new_request.add_keywords(req->keywords(i));
            }
        }
        if(new_request.keywords_size() <= 0) {
            searchResult->Finish();
            return;
        }

        new_request.add_context_vector(req->context_vector(0));
        new_request.add_context_vector(req->context_vector(1));
        new_request.set_hour(req->hour());
        new_request.set_topn(req->topn());

        AsyncClientCall* call = new AsyncClientCall;
        call->searchResult = searchResult;
        call->response_reader = stub_->PrepareAsyncInernalSearch(&call->context, new_request, &cq_);
        call->response_reader->StartCall();
        call->response_reader->Finish(&call->response, &call->status, (void*)call);
    }   

private:
    void HandleResponse(SearchResponse* response, AyncSearchResult* searchResult, Status status)  {
        if(!status.ok()) {
            searchResult->Cancel();
            return;
        }
        searchResult->results.reserve(response->adgroup_ids_size());
        for (int i = 0; i < response->adgroup_ids_size(); i++) {
            searchResult->results.emplace_back(
                response->adgroup_ids(i), 
                response->prices(i), 
                response->ctrs(i), 
                response->scores(i)
            );
            // std::cout << "HandleResponse: " << searchResult->results[i] << std::endl;
        }
        searchResult->Finish();
    }

private:
    Options options;

    struct AsyncClientCall {
        SearchResponse response;
        AyncSearchResult* searchResult;
        ClientContext context;
        Status status;
        std::unique_ptr<ClientAsyncResponseReader<SearchResponse>> response_reader;
    };
    CompletionQueue cq_;


    std::thread* bg_thread;

    std::unique_ptr<SearchService::Stub> stub_;
};