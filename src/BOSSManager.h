#pragma once
#include "Common.h"
#include "CombatSearch.h"
#include "ActionType.h"
#include "BuildOrderAbilities.h"
#include "CombatSearchParameters.h"

using namespace CCUnit;

class CCBot;

class BOSSManager
{
    CCBot &     m_bot;
    BOSS::GameState   m_currentGameState;
    std::vector<BOSS::Unit> m_currentUnits;

    void setCurrentGameState();
    void setCurrentUnits(const std::vector<CCUnit::Unit> & CCUnits);

public:
    BOSSManager(CCBot & bot);

    void setParameters(int frameLimit, float timeLimit, bool alwaysMakeWorkers,
                        const std::vector<std::pair<BOSS::ActionType, int>> & maxActions,
                        const BOSS::BuildOrderAbilities & openingBuildOrder,
                        const BOSS::ActionSetAbilities & relevantActions) const;

    
};

