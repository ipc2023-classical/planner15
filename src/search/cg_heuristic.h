#ifndef CG_HEURISTIC_H
#define CG_HEURISTIC_H

#include "heuristic.h"
#include "algorithms/priority_queues.h"

#include <string>
#include <vector>

class CGCache;
class DomainTransitionGraph;
class GlobalState;
struct ValueNode;

class CGHeuristic : public Heuristic {
    std::vector<priority_queues::AdaptiveQueue<ValueNode *> *> prio_queues;

    CGCache *cache;
    int cache_hits;
    int cache_misses;

    int helpful_transition_extraction_counter;

    void setup_domain_transition_graphs();
    int get_transition_cost(const GlobalState &state, DomainTransitionGraph *dtg, int start_val, int goal_val);
    void mark_helpful_transitions(const GlobalState &state, DomainTransitionGraph *dtg, int to);
protected:
    virtual void initialize();
    virtual int compute_heuristic(const GlobalState &state);
public:
    CGHeuristic(const options::Options &opts);
    ~CGHeuristic();
    virtual bool dead_ends_are_reliable() const;
};

#endif
