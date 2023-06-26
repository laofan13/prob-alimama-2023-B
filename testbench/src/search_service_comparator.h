#pragma once

#include <cmath>
#include <algorithm>
#include <random>
#include <atomic>

#include "grpc_benchmark.h"

#include "defines.h"

constexpr int32_t kSampleStepSize {100};

void RandomSelect(float percent, std::array<bool, kSampleStepSize> &select) {
    std::random_device rd;
    std::mt19937 g(rd());

    std::array<int, kSampleStepSize> indices;
    std::iota(indices.begin(), indices.end(), 0);

    std::shuffle(indices.begin(), indices.end(), g);

    int32_t position = int32_t (percent * kSampleStepSize);
    for (int i = 0; i < position; ++i) {
        select[indices[i]] = true;
    }
}

class SearchServiceComparatorDummy : public SearchServiceGprcBenchmark::Comparator {
public:
    bool Compare(const ResponsePtr& resp, const ResponsePtr& ref, CustomSummary& result) override {
        return true;
    }
};

class SearchServiceComparatorSample : public SearchServiceGprcBenchmark::Comparator {
public:
    SearchServiceComparatorSample(double sample_percent_th, int32_t sample_score_th, bool auto_interrupt=false):
        sample_percent_th_{sample_percent_th}, sample_score_th_{sample_score_th},
        random_select_{}, index_{0}, auto_interrupt_{auto_interrupt} {
        RandomSelect(sample_percent_th_, random_select_);
    }

    bool Compare(const ResponsePtr& resp, const ResponsePtr& ref, CustomSummary& result) override {
        auto idx_cur = index_.fetch_add(1);
        size_t idx = idx_cur % kSampleStepSize;
        if (random_select_.at(idx) == false) {
            return true;
        }
        if (!resp) {
            BOOST_LOG_TRIVIAL(error)  << "resp is null ";
            return false;
        }
        if (!ref) {
            BOOST_LOG_TRIVIAL(error)  << "ref is null ";
            return false;
        }
        if (ref->adgroup_ids().size() == 0) {
            BOOST_LOG_TRIVIAL(error)  << "reference adgroup_ids is empty ";
            return false;
        }

        int32_t score {0};
        do {
            int32_t topNCheckNum = static_cast<int32_t>(std::ceil(g_config.accuracy_th * ref->adgroup_ids().size()));
            if (resp->adgroup_ids().size() < topNCheckNum) { // 集合数量不一致
                break;
            };

            auto ref_iter_start = ref->adgroup_ids().begin();
            auto ref_iter_end = ref->adgroup_ids().begin() + topNCheckNum;
            auto resp_iter_start = resp->adgroup_ids().begin();
            auto resp_iter_end = resp->adgroup_ids().begin() + topNCheckNum;
            if (std::equal(resp_iter_start, resp_iter_end, ref_iter_start, ref_iter_end)) { // 集合+顺序完全一致时
                result.ad_correct_num ++;
                score += 80;
            } else if (std::is_permutation(resp_iter_start, resp_iter_end, ref_iter_start, ref_iter_end)) { // 集合完全一致时，顺序不一致
                result.ad_partial_correct_num ++;
                score += 50;
                break;
            } else { // 集合不一致时
                break;
            }

            auto ref_price_start = ref->prices().begin();
            auto ref_price_end = ref->prices().begin() + topNCheckNum;
            auto resp_price_start = resp->prices().begin();
            auto resp_price_end = resp->prices().begin() + topNCheckNum;
            if (std::equal(ref_price_start, ref_price_end, resp_price_start, resp_price_end)) {
                result.price_correct_num ++;
                score += 20;
            }
        } while(0);
        result.total_num ++;
        result.total_score += score;
        return (score >= sample_score_th_) || !auto_interrupt_;
    }

private:
    bool auto_interrupt_;
    double sample_percent_th_;
    int32_t sample_score_th_;
    std::array<bool, 100> random_select_;
    std::atomic<uint64_t> index_;
};

class SearchServiceComparatorAll : public SearchServiceGprcBenchmark::Comparator {
public:
    bool Compare(const ResponsePtr& resp, const ResponsePtr& ref, CustomSummary& result) override {
        if (!resp) {
            BOOST_LOG_TRIVIAL(error)  << "resp is null ";
            return false;
        }
        if (!ref) {
            BOOST_LOG_TRIVIAL(error)  << "ref is null ";
            return false;
        }
        // Check adgroup_ids
        // 要求集合正确性超过阈值
        if (ref->adgroup_ids().size() == 0) {
            BOOST_LOG_TRIVIAL(error)  << "reference adgroup_ids is empty ";
            return false;
        }
        result.total_num ++;

        int32_t topNCheckNum = static_cast<int32_t>(std::ceil(g_config.accuracy_th * ref->adgroup_ids().size()));
        if (resp->adgroup_ids().size() < topNCheckNum) { // 集合数量不一致
            return true;
        };

        auto ref_iter_start = ref->adgroup_ids().begin();
        auto ref_iter_end = ref->adgroup_ids().begin() + topNCheckNum;
        auto resp_iter_start = resp->adgroup_ids().begin();
        auto resp_iter_end = resp->adgroup_ids().begin() + topNCheckNum;
        if (std::equal(resp_iter_start, resp_iter_end, ref_iter_start, ref_iter_end)) { // 集合+顺序完全一致时
            result.ad_correct_num ++;
            result.total_score += 50;
            result.total_score += 30;
        } else if (std::is_permutation(resp_iter_start, resp_iter_end, ref_iter_start, ref_iter_end)) { // 集合完全一致时，顺序不一致
            result.ad_partial_correct_num ++;
            result.total_score += 50;
            return true;
        } else { // 集合不一致时
            return true;
        }

        auto ref_price_start = ref->prices().begin();
        auto ref_price_end = ref->prices().begin() + topNCheckNum;
        auto resp_price_start = resp->prices().begin();
        auto resp_price_end = resp->prices().begin() + topNCheckNum;
        if (std::equal(ref_price_start, ref_price_end, resp_price_start, resp_price_end)) {
            result.price_correct_num ++;
            result.total_score += 20;
        }
        return true;
    }
};