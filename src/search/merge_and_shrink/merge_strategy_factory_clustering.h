#ifndef MERGE_AND_SHRINK_MERGE_STRATEGY_FACTORY_CLUSTERING_H
#define MERGE_AND_SHRINK_MERGE_STRATEGY_FACTORY_CLUSTERING_H

#include "merge_strategy_factory.h"

namespace utils {
class RandomNumberGenerator;
}

namespace merge_and_shrink {
class Clustering;
class MergeTreeFactory;
class MergeSelector;

enum class OrderOfClustering {
    RANDOM,
    GIVEN,
    REVERSE,
    DECREASING,
    INCREASING
};

class MergeStrategyFactoryClustering : public MergeStrategyFactory {
    std::shared_ptr<Clustering> clustering_factory;
    OrderOfClustering order_of_clusters;
    std::shared_ptr<utils::RandomNumberGenerator> rng;
    std::shared_ptr<MergeTreeFactory> merge_tree_factory;
    std::shared_ptr<MergeSelector> merge_selector;
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    MergeStrategyFactoryClustering(const options::Options &options);
    virtual ~MergeStrategyFactoryClustering() override = default;
    virtual std::unique_ptr<MergeStrategy> compute_merge_strategy(
        const TaskProxy &task_proxy,
        const FactoredTransitionSystem &fts) override;
    virtual bool requires_init_distances() const override;
    virtual bool requires_goal_distances() const override;
};
}

#endif
