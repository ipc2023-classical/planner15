#include "globals.h"

#include "algorithms/int_packer.h"
#include "axioms.h"
#include "compliant_paths/compliant_path_graph.h"
#include "compliant_paths/cpg_storage.h"
#include "compliant_paths/explicit_state_cpg.h"
#include "task_utils/domain_transition_graph.h"
#include "factoring.h"
#include "heuristic.h"
#include "leaf_state_id.h"
#include "operator.h"
#include "state.h"
#include "state_registry.h"
#include "symmetries/graph_creator.h"
#include "tasks/root_task.h"
#include "task_utils/successor_generator.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <sstream>

using namespace std;


static const int PRE_FILE_VERSION = 3;


// TODO: This needs a proper type and should be moved to a separate
//       mutexes.cc file or similar, accessed via something called
//       g_mutexes. (Right now, the interface is via global function
//       are_mutex, which is at least better than exposing the data
//       structure globally.)

static vector<vector<set<pair<int, int> > > > g_inconsistent_facts;

bool test_goal(const GlobalState &state) {
    for (size_t i = 0; i < g_goal.size(); ++i) {
        assert(!g_factoring || g_belongs_to_factor[g_goal[i].first] == LeafFactorID::CENTER);
        if (state[g_goal[i].first] != g_goal[i].second){
            return false;
        }
    }
    if (g_factoring){
        const CompliantPathGraph *cpg = CPGStorage::storage->get_cpg(state);
        for (LeafFactorID factor(0); factor < g_leaves.size(); ++factor) {
            if (!g_goals_per_factor[factor].empty() && !cpg->goal_reachable(factor)){
                return false;
            }
        }
    }
    return true;
}

int calculate_plan_cost(const vector<OperatorID> &plan) {
    // TODO: Refactor: this is only used by save_plan (see below)
    //       and the SearchEngine classes and hence should maybe
    //       be moved into the SearchEngine (along with save_plan).
    int plan_cost = 0;
    for (OperatorID id : plan) {
        plan_cost += g_operators[id].get_cost();
    }
    return plan_cost;
}

void save_plan(const vector<OperatorID> &plan,
               bool generates_multiple_plan_files) {
    // TODO: Refactor: this is only used by the SearchEngine classes
    //       and hence should maybe be moved into the SearchEngine.
    ostringstream filename;
    filename << g_plan_filename;
    int plan_number = g_num_previously_generated_plans + 1;
    if (generates_multiple_plan_files || g_is_part_of_anytime_portfolio) {
        filename << "." << plan_number;
    } else {
        assert(plan_number == 1);
    }
    ofstream outfile(filename.str());
    for (OperatorID id : plan) {
        cout << g_operators[id].get_name() << " (" << g_operators[id].get_cost() << ")" << endl;
        outfile << "(" << g_operators[id].get_name() << ")" << endl;
    }
    int plan_cost = calculate_plan_cost(plan);
    outfile << "; cost = " << plan_cost << " ("
            << (is_unit_cost() ? "unit cost" : "general cost") << ")" << endl;
    outfile.close();
    cout << "Plan length: " << plan.size() << " step(s)." << endl;
    cout << "Plan cost: " << plan_cost << endl;
    ++g_num_previously_generated_plans;
}

bool peek_magic(istream &in, string magic) {
    string word;
    in >> word;
    bool result = (word == magic);
    for (int i = word.size() - 1; i >= 0; --i)
        in.putback(word[i]);
    return result;
}

void check_magic(istream &in, string magic) {
    string word;
    in >> word;
    if (word != magic) {
        cout << "Failed to match magic word '" << magic << "'." << endl;
        cout << "Got '" << word << "'." << endl;
        if (magic == "begin_version") {
            cerr << "Possible cause: you are running the planner "
                 << "on a preprocessor file from " << endl
                 << "an older version." << endl;
        }
        exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}

void read_and_verify_version(istream &in) {
    int version;
    check_magic(in, "begin_version");
    in >> version;
    check_magic(in, "end_version");
    if (version != PRE_FILE_VERSION) {
        cerr << "Expected preprocessor file version " << PRE_FILE_VERSION
             << ", got " << version << "." << endl;
        cerr << "Exiting." << endl;
        exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}

void read_metric(istream &in) {
    check_magic(in, "begin_metric");
    in >> g_use_metric;
    check_magic(in, "end_metric");
}

void read_variables(istream &in) {
    int count;
    in >> count;
    for (int i = 0; i < count; ++i) {
        check_magic(in, "begin_variable");
        string name;
        in >> name;
        g_variable_name.push_back(name);
        int layer;
        in >> layer;
        g_axiom_layers.push_back(layer);
        int range;
        in >> range;
        g_variable_domain.push_back(range);
        in >> ws;
        vector<string> fact_names(range);
        for (size_t j = 0; j < fact_names.size(); ++j)
            getline(in, fact_names[j]);
        g_fact_names.push_back(fact_names);
        check_magic(in, "end_variable");
    }
}

void read_mutexes(istream &in) {
    g_inconsistent_facts.resize(g_variable_domain.size());
    for (size_t i = 0; i < g_variable_domain.size(); ++i)
        g_inconsistent_facts[i].resize(g_variable_domain[i]);

    int num_mutex_groups;
    in >> num_mutex_groups;

    /* NOTE: Mutex groups can overlap, in which case the same mutex
       should not be represented multiple times. The current
       representation takes care of that automatically by using sets.
       If we ever change this representation, this is something to be
       aware of. */

    for (int i = 0; i < num_mutex_groups; ++i) {
        check_magic(in, "begin_mutex_group");
        int num_facts;
        in >> num_facts;
        vector<pair<int, int> > invariant_group;
        invariant_group.reserve(num_facts);
        for (int j = 0; j < num_facts; ++j) {
            int var, val;
            in >> var >> val;
            invariant_group.push_back(make_pair(var, val));
        }
        check_magic(in, "end_mutex_group");
        for (size_t j = 0; j < invariant_group.size(); ++j) {
            const pair<int, int> &fact1 = invariant_group[j];
            int var1 = fact1.first, val1 = fact1.second;
            for (size_t k = 0; k < invariant_group.size(); ++k) {
                const pair<int, int> &fact2 = invariant_group[k];
                int var2 = fact2.first;
                if (var1 != var2) {
                    /* The "different variable" test makes sure we
                       don't mark a fact as mutex with itself
                       (important for correctness) and don't include
                       redundant mutexes (important to conserve
                       memory). Note that the preprocessor removes
                       mutex groups that contain *only* redundant
                       mutexes, but it can of course generate mutex
                       groups which lead to *some* redundant mutexes,
                       where some but not all facts talk about the
                       same variable. */
                    g_inconsistent_facts[var1][val1].insert(fact2);
                }
            }
        }
    }
}

void read_goal(istream &in) {
    check_magic(in, "begin_goal");
    int count;
    in >> count;
    if (count < 1) {
        cerr << "Task has no goal condition!" << endl;
        exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    for (int i = 0; i < count; ++i) {
        int var, val;
        in >> var >> val;
        g_goal.push_back(make_pair(var, val));
    }
    check_magic(in, "end_goal");
}

void dump_goal() {
    cout << "Goal Conditions:" << endl;
    for (size_t i = 0; i < g_goal.size(); ++i)
        cout << "  " << g_variable_name[g_goal[i].first] << ": "
             << g_goal[i].second << endl;
}

void read_operators(istream &in) {
    int count;
    in >> count;
    for (int i = 0; i < count; ++i)
        g_operators.push_back(Operator(in, false));
}

void read_axioms(istream &in) {
    int count;
    in >> count;
    for (int i = 0; i < count; ++i)
        g_axioms.push_back(Operator(in, true));

    g_axiom_evaluator = new AxiomEvaluator;
}

void read_everything(istream &in) {
    cout << "reading input... [t=" << utils::g_timer << "]" << endl;
    read_and_verify_version(in);
    read_metric(in);
    read_variables(in);
    read_mutexes(in);
    g_initial_state_data.resize(g_variable_domain.size());
    check_magic(in, "begin_state");
    for (size_t i = 0; i < g_variable_domain.size(); ++i) {
        in >> g_initial_state_data[i];
    }
    check_magic(in, "end_state");
    g_default_axiom_values = g_initial_state_data;

    read_goal(in);
    read_operators(in);
    read_axioms(in);

    cout << "done reading input! [t=" << utils::g_timer << "]" << endl;

    // TODO: this has to happen *before* factoring does anything in order to
    // keep a copy of g_goal.
    tasks::create_root_task();


    if (g_factoring){
        g_factoring->do_factoring_or_abstain();
        if (g_factoring->reset_factoring()){
            g_factoring.reset();
        }
    }

    assert(!g_variable_domain.empty());
    if (g_factoring){
        vector<int> center_domains;
        vector<vector<int> > leaf_domains(g_leaves.size());
        for (size_t var = 0; var < g_belongs_to_factor.size(); var++){
            LeafFactorID factor = g_belongs_to_factor[var];
            if (factor == LeafFactorID::CENTER){
                center_domains.push_back(g_variable_domain[var]);
            } else {
                if (leaf_domains[factor].empty()){
                    leaf_domains[factor].resize(g_leaves[factor].size(), -1);
                }
                leaf_domains[factor][g_new_index[var]] = g_variable_domain[var];
            }
        }

        // TODO further adapt this based on experiments
        // TODO it can probably be somewhat aligned with the bin-packing, too.
        // if we need a new bucket anyway, then we could as well use an entire int
        if (CompliantPathGraph::get_pruning_options().use_exact_duplicate_checking()){
            // numbers are slightly higher than so-far observed maximum
            switch (g_factoring->get_search_type()){
            case ASDA:
            case SDA: MAX_DUPLICATE_COUNTER = 1500000; break;
            case SAT: MAX_DUPLICATE_COUNTER = 400000; break;
            case UNSAT: MAX_DUPLICATE_COUNTER = 2000000; break;

            default: cerr << "Unknown decoupled search type." << endl;
                     exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
            }
        } else {
            // numbers are roughly double than so-far observed maximum
            switch (g_factoring->get_search_type()){
            case ASDA:
            case SDA: MAX_DUPLICATE_COUNTER = 50000; break;
            case SAT: MAX_DUPLICATE_COUNTER = 100000; break;
            case UNSAT: MAX_DUPLICATE_COUNTER = 50000; break;

            default: cerr << "Unknown decoupled search type." << endl;
                     exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
            }
        }

        center_domains.push_back(MAX_DUPLICATE_COUNTER);

        cout << "packing state variables..." << flush;
        g_state_packer = new int_packer::IntPacker(center_domains);
        cout << "Variables: " << g_center.size() << endl;
        g_leaf_state_packers = vector<int_packer::IntPacker*>(g_leaves.size(), 0);
        for (size_t i = 0; i < g_leaf_state_packers.size(); i++){
            g_leaf_state_packers[i] = new int_packer::IntPacker(leaf_domains[i]);
        }
    } else {
        g_state_packer = new int_packer::IntPacker(g_variable_domain);
        g_leaf_state_packers.push_back(new int_packer::IntPacker(vector<int>(1, 2)));
        cout << "Variables: " << g_variable_domain.size() << endl;
    }

    cout << "Bytes per state: "
         << g_state_packer->get_num_bins() * sizeof(int_packer::IntPacker::Bin) << endl;

    if (g_factoring){
        for (LeafFactorID factor(0); factor < g_leaves.size(); ++factor){
            cout << "Bytes per state of leaf factor " << factor << ": "
                    << g_leaf_state_packers[factor]->get_num_bins() * sizeof(int_packer::IntPacker::Bin) << endl;
        }
    }

    cout << "Building successor generator..." << flush;
    int peak_memory_before = utils::get_peak_memory_in_kb();
    utils::Timer successor_generator_timer;
    g_successor_generator = new successor_generator::SuccessorGenerator();
    if (g_factoring && g_factoring->get_leaf_representation_type() == EXPLICIT){
        ExplicitStateCPG::build_leaf_successor_generators();
    }
    successor_generator_timer.stop();
    cout << "done! [t=" << utils::g_timer << "]" << endl;
    int peak_memory_after = utils::get_peak_memory_in_kb();
    int memory_diff = peak_memory_after - peak_memory_before;
    cout << "peak memory difference for root successor generator creation: "
            << memory_diff << " KB" << endl
            << "time for root successor generation creation: "
            << successor_generator_timer << endl;

    cout << "done! [t=" << utils::g_timer << "]" << endl;

    // NOTE: state registry stores the sizes of the state, so must be
    // built after the problem has been read in.
    g_state_registry = new StateRegistry;

    cout << "done initalizing global data [t=" << utils::g_timer << "]" << endl;
}

void dump_everything() {
    cout << "Use metric? " << g_use_metric << endl;
    cout << "Min Action Cost: " << g_min_action_cost << endl;
    cout << "Max Action Cost: " << g_max_action_cost << endl;
    // TODO: Dump the actual fact names.
    cout << "Variables (" << g_variable_name.size() << "):" << endl;
    for (size_t i = 0; i < g_variable_name.size(); ++i)
        cout << "  " << g_variable_name[i]
             << " (range " << g_variable_domain[i] << ")" << endl;
    GlobalState initial_state = g_initial_state();
    cout << "Initial GlobalState (PDDL):" << endl;
    initial_state.dump_pddl();
    cout << "Initial GlobalState (FDR):" << endl;
    initial_state.dump_fdr();
    dump_goal();
    /*
    cout << "Successor Generator:" << endl;
    g_successor_generator->dump();
    for(int i = 0; i < g_variable_domain.size(); ++i)
      g_transition_graphs[i]->dump();
    */
}

bool is_unit_cost() {
    return g_min_action_cost == 1 && g_max_action_cost == 1;
}

bool has_axioms() {
    return !g_axioms.empty();
}

void verify_no_axioms() {
    if (has_axioms()) {
        cerr << "Heuristic does not support axioms!" << endl << "Terminating."
             << endl;
        exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
    }
}

static int get_first_conditional_effects_op_id() {
    for (size_t i = 0; i < g_operators.size(); ++i) {
        const vector<Effect> &effects = g_operators[i].get_effects();
        for (size_t j = 0; j < effects.size(); ++j) {
            const vector<Condition> &cond = effects[j].conditions;
            if (!cond.empty())
                return i;
        }
    }
    return -1;
}

bool has_conditional_effects() {
    return get_first_conditional_effects_op_id() != -1;
}

void verify_no_conditional_effects() {
    int op_id = get_first_conditional_effects_op_id();
    if (op_id != -1) {
        cerr << "Heuristic does not support conditional effects "
             << "(operator " << g_operators[op_id].get_name() << ")" << endl
             << "Terminating." << endl;
        exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
    }
}

void verify_no_axioms_no_conditional_effects() {
    verify_no_axioms();
    verify_no_conditional_effects();
}

bool are_mutex(const pair<int, int> &a, const pair<int, int> &b) {
    if (a.first == b.first) // same variable: mutex iff different value
        return a.second != b.second;
    return bool(g_inconsistent_facts[a.first][a.second].count(b));
}

const GlobalState &g_initial_state() {
    return g_state_registry->get_initial_state();
}

bool g_use_metric;
int g_min_action_cost = numeric_limits<int>::max();
int g_max_action_cost = 0;

int MAX_DUPLICATE_COUNTER;

shared_ptr<Factoring> g_factoring;

int g_inc_g_by;

vector<string> g_variable_name;
vector<int> g_variable_domain;

vector<int> g_center;
vector<vector<int> > g_leaves;
vector<LeafFactorID> g_belongs_to_factor;
vector<size_t> g_new_index;

shared_ptr<symmetries::GraphCreator> g_symmetry_graph;

vector<vector<string> > g_fact_names;
vector<int> g_axiom_layers;
vector<int> g_default_axiom_values;
int_packer::IntPacker *g_state_packer;
vector<int_packer::IntPacker*> g_leaf_state_packers;

vector<int> g_initial_state_data;

vector<pair<int, int> > g_goal;
vector<vector<pair<int, int> > > g_goals_per_factor;

vector<Operator> g_operators;
vector<Operator> g_axioms;
AxiomEvaluator *g_axiom_evaluator;
successor_generator::SuccessorGenerator *g_successor_generator;
vector<DomainTransitionGraph *> g_transition_graphs;

string g_plan_filename = "sas_plan";
bool g_is_part_of_anytime_portfolio = false;
int g_num_previously_generated_plans = 0;

StateRegistry *g_state_registry = 0;
