#pragma once
// CC files
#include "Common.h"
#include "Unit.h"
#include "BuildOrder.h"
#include "BuildingManager.h"
#include "BuildOrderQueue.h"

// BOSS files
#include "CombatSearch.h"
#include "ActionType.h"
#include "BuildOrderAbilities.h"
#include "CombatSearchParameters.h"
#include "CombatSearch_Integral.h"
#include "CombatSearch_IntegralMCTS.h"
#include "NMCS.h"

#include <atomic>

namespace CC
{
    class CCBot;

    class BOSSManager
    {
    public:
        enum SearchState
        {
            Free, Searching, GettingResults, ExitSearch
        };

        BuildOrderQueue                     m_queue;

    private:
        CCBot &                             m_bot;
        std::unique_ptr<BOSS::CombatSearch> m_searcher;
        BuildingManager &                   m_buildingManager;

        std::atomic<int>                    m_searchState;

        BOSS::GameState                     m_futureGameState;
        BOSS::BuildOrderAbilities           m_currentBuildOrder;
        std::vector<int>                    m_enemyUnits;

        BOSS::GameState                     m_currentGameState;
        BOSS::BuildOrderAbilities           m_previousBuildOrder;

        BOSS::CombatSearchParameters        m_params;
        std::thread                         m_searchThread;

        BOSS::CombatSearchResults           m_results;
        std::vector<BOSS::CombatSearchResults> m_searchResults;
        std::string                         m_unitInfo;

        bool                                m_fastReaction;
        bool                                m_deadUnit;
        bool                                m_needFastReaction;

        const static int enemyArmyBeforeReact = 1;
        const static int frameLimit = 6720;
        const static int frameBuildOrderUse = frameLimit / 2;
        const static int frameLimitFastReaction = frameBuildOrderUse / 2;

        void initializeParameters();

        void setBuildOrder(const BuildOrder& buildOrder);
        void printDebugInfo() const;

        std::vector<BOSS::Unit> getCurrentUnits();
        bool setEnemyUnits();

        void getResult();
        void storeBuildOrderInfo(const BOSS::ActionAbilityPair& action, const BOSS::GameState& state);

        void queueEmpty();

        void newEnemyUnit();
        void newEnemyUnitFastReaction(int startingIndex);
        void newEnemyUnitsMediumReaction();
        void newEnemyUnitSlowReaction();

        void unitsDied(const std::vector<Unit> & deadUnits);
        void unitsDiedFastReaction(const std::vector<Unit> & deadUnits, int startingIndex);
        void unitsDiedSlowReaction(const std::vector<Unit>& deadUnits);

        void addToQueue(const BOSS::BuildOrderAbilities& buildOrder);

        void threadSearch();

    public:
        BOSSManager(CCBot & bot, BuildingManager& buildingManager);

        void onStart();
        void onFrame();

        void setParameters(bool reset);
        void setCurrentGameState(bool reset);
        void startSearch();

        void finishSearch();

        void doBuildOrder(const BuildOrder & inputBuildOrder);
        void doBuildOrder(BOSS::BuildOrderAbilities& buildOrder);
        void doBuildingsInQueue(BOSS::GameState& state) const;
        // fixes chronoboost targetting after creating a new GameState from the actual game
        void fixBuildOrder(const BOSS::GameState & state, BOSS::BuildOrderAbilities & buildOrder, int startingIndex, bool doTheBuildingsInQueue = true);
        bool fixBuildOrderRecurse(const BOSS::GameState & state, BOSS::BuildOrderAbilities & buildOrder, int index) const;
    };
}