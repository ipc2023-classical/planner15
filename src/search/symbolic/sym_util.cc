#include "sym_util.h"


namespace symbolic {

#ifdef USE_CUDD

TransitionRelation mergeTR(TransitionRelation tr, const TransitionRelation &tr2, int maxSize) {
    tr.merge(tr2, maxSize);
    return tr;
}
BDD mergeAndBDD(const BDD &bdd, const BDD &bdd2, int maxSize) {
    return bdd.And(bdd2, maxSize);
}
BDD mergeOrBDD(const BDD &bdd, const BDD &bdd2, int maxSize) {
    return bdd.Or(bdd2, maxSize);
}
ADD mergeSumADD(const ADD &add, const ADD &add2, int ) {
    return add + add2;
}

ADD mergeMaxADD(const ADD &add, const ADD &add2, int ) {
    return add.Maximum(add2);
}

#endif

}

