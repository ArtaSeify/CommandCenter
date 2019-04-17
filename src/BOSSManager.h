#pragma once
// CC files
#include "Common.h"
#include "Unit.h"

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
        enum SearchMessage
        {
            None, QueueEmpty, UnitDied, NewEnemyUnit
        };

    private:
        CCBot &                             m_bot;
        std::unique_ptr<BOSS::CombatSearch> m_searcher;

        BOSS::GameState                     m_currentGameState;
        std::vector<std::pair<std::string, std::string>> m_unitStartTimes;
        std::vector<BOSS::Unit>             m_currentUnits;
        std::vector<int>                    m_enemyUnits;

        BOSS::CombatSearchParameters        m_params;
        BOSS::CombatSearchResults           m_results;
        std::thread                         m_searchThread;
        std::vector<BOSS::CombatSearchResults> m_searchResults;

        std::atomic<int>                    m_searchState;
        int                                 m_largestFrameSearched;

        void printDebugInfo() const;
        void setCurrentUnits(const std::vector<Unit> & CCUnits);
        void setEnemyUnits();
        void getResult();
        void storeUnitStartTime(const BOSS::ActionAbilityPair& action);
        void queueEmpty();
        void unitDied();
        void threadSearch();

    public:
        BOSSManager(CCBot & bot);

        void onStart();
        void onFrame(SearchMessage message);

        void setParameters(bool reset);
        void setCurrentGameState(bool reset);
        void startSearch(bool reset);

        void finishSearch(SearchMessage message);

        const BOSS::BuildOrderAbilities & getBuildOrder();
        void setOpeningBuildOrder();

        int  highestFrameSearched() const   { return m_largestFrameSearched; }
    };
}