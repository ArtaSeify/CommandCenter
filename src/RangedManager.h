#pragma once

#include "Common.h"
#include "MicroManager.h"

namespace CC
{
    class CCBot;

    class RangedManager : public MicroManager
    {
    public:

        RangedManager(CCBot & bot);
        void    executeMicro(const std::vector<Unit> & targets);
        void    assignTargets(const std::vector<Unit> & targets);
        int     getAttackPriority(const Unit & attacker, const Unit & enemyUnit);
        Unit    getTarget(const Unit & rangedUnit, const std::vector<Unit> & targets);
    };
}