#ifndef LAZY_SEARCH_H
#define LAZY_SEARCH_H

#include "state.h"
#include "evaluator.h"
#include "search_engine.h"
#include "search_progress.h"
#include "search_space.h"

#include "open_lists/open_list.h"

#include <vector>

class Operator;
class Heuristic;

namespace options {
class Options;
}

typedef std::pair<StateID, OperatorID> OpenListEntryLazy;

class LazySearch : public SearchEngine {
protected:
    std::shared_ptr<OpenList<OpenListEntryLazy>> open_list;

    // Search Behavior parameters
    bool reopen_closed_nodes; // whether to reopen closed nodes upon finding lower g paths
    bool randomize_successors;
    bool preferred_successors_first;

    std::vector<Heuristic*> heuristics;
    std::vector<std::shared_ptr<Evaluator>> preferred_operator_heuristics;
    std::vector<Heuristic*> estimate_heuristics;

    GlobalState current_state;
    StateID current_predecessor_id;
    OperatorID current_operator;
    int current_g;
    int current_real_g;

    virtual void initialize();
    virtual SearchStatus step();

    void generate_successors();
    SearchStatus fetch_next_state();

    void reward_progress();

    void get_successor_operators(std::vector<OperatorID> &ops);
public:

    LazySearch(const options::Options &opts);

    void set_pref_operator_heuristics(std::vector<std::shared_ptr<Evaluator>> &heur);

    virtual void statistics() const;
};

#endif
