#include "assist/Session.h"
using namespace ia2a;
int main() {
    Session s;
    const auto original=s.applied();
    Settings proposal=original; proposal.enabled=true; proposal.mid.airDb=2;
    auto first=s.begin();
    if(!s.offer(first,proposal,-2)||s.applied()!=original||s.seed()!=proposal) return 1;
    auto correction=s.begin();
    if(s.offer(first,original,0)||s.seed()!=proposal) return 2;
    proposal.mid.airDb=1;
    if(!s.offer(correction,proposal,-1)) return 3;
    s.listen(false,false); if(s.monitor().preview) return 4;
    s.listen(true,true); if(!s.monitor().preview||s.monitor().gainDb!=-1) return 5;
    if(!s.apply()||s.applied()!=proposal||s.pending()||!s.undo()||s.applied()!=original) return 6;
    auto late=s.begin(); s.invalidate(); if(s.offer(late,proposal,0))return 7;
    auto again=s.begin();s.offer(again,proposal,-2);s.discard();if(s.applied()!=original||s.pending())return 8;
}
