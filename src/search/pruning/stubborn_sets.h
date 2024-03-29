#ifndef STUBBORN_SETS_H
#define STUBBORN_SETS_H

#include "../pruning_method.h"

namespace options {
class OptionParser;
class Options;
}

namespace stubborn_sets {
class StubbornSets : public PruningMethod {
    long num_unpruned_successors_generated;
    long num_pruned_successors_generated;
    int expanded_states;
    bool disabled_pruning;
    double min_pruning_ratio;

    /* stubborn[op_no] is true iff the operator with operator index
       op_no is contained in the stubborn set */
    std::vector<bool> stubborn;

    /*
      stubborn_queue contains the operator indices of operators that
      have been marked as stubborn but have not yet been processed
      (i.e. more operators might need to be added to stubborn because
      of the operators in the queue).
    */
    std::vector<OperatorID> stubborn_queue;

    void compute_sorted_operators();
    void compute_achievers();

protected:
    std::vector<std::vector<std::pair<int, int>>> sorted_op_preconditions;
    std::vector<std::vector<std::pair<int, int>>> sorted_op_effects;

    /* achievers[var][value] contains all operator indices of
       operators that achieve the fact (var, value). */
    std::vector<std::vector<std::vector<OperatorID>>> achievers;

    bool can_disable(OperatorID op1_no, OperatorID op2_no);
    bool can_conflict(OperatorID op1_no, OperatorID op2_no);
    bool contain_conflicting_fact(const std::vector<std::pair<int, int>> &facts1,
                                  const std::vector<std::pair<int, int>> &facts2);

    // Returns true iff the operators was enqueued.
    // TODO: rename to enqueue_stubborn_operator?
    virtual bool mark_as_stubborn(OperatorID op_no);
    virtual void initialize_stubborn_set(const GlobalState &state) = 0;
    virtual void handle_stubborn_operator(const GlobalState &state, OperatorID op_no) = 0;
public:
    StubbornSets(const options::Options &opts);
    virtual ~StubbornSets() = default;

    virtual void initialize() override = 0;

    /* TODO: move prune_operators, and also the statistics, to the
       base class to have only one method virtual, and to make the
       interface more obvious */
    virtual void prune_operators(const GlobalState &state,
                                 std::vector<OperatorID> &ops) override;
    virtual void print_statistics() const override;

    static void add_options_to_parser(options::OptionParser &parser);
};
}

#endif
