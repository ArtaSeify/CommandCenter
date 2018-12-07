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

namespace CC
{
    class CCBot;

    class BOSSManager
    {
        CCBot &                             m_bot;
        BOSS::CombatSearch_Integral         m_searcher;
        BOSS::GameState                     m_currentGameState;
        BOSS::Vector_Unit                   m_currentUnits;
        BOSS::CombatSearchParameters        m_params;
        BOSS::CombatSearchResults           m_results;
        std::thread                         m_searchThread;

        bool m_searching;
        bool m_searchFinished;
        int  m_largestFrameSearched;

        void setCurrentUnits(const std::vector<Unit> & CCUnits);
        void searchFinished();
        void threadSearch();

    public:
        BOSSManager(CCBot & bot);

        void setParameters(int frameLimit, float timeLimit, bool alwaysMakeWorkers, bool sortActions,
            const std::vector<std::pair<BOSS::ActionType, int>> & maxActions,
            const BOSS::BuildOrderAbilities & openingBuildOrder,
            const BOSS::ActionSetAbilities & relevantActions);

        void setCurrentGameState();
        void startSearch();
        void finishSearch();

        const BOSS::BuildOrderAbilities & BOSSManager::getBuildOrder();
        void setOpeningBuildOrder();

        BOSS::RaceID getBOSSPlayerRace() const;
        int  numSupplyProviders() const;

        //bool searchInProgress() const   { return m_searching; }
        bool searchInProgress() const       { return m_searchThread.joinable(); }
        bool isSearchFinished() const       { return m_searchFinished; }
        void gotData()                      { m_searchFinished = false; }
        int  highestFrameSearched() const   { return m_largestFrameSearched; }
              
    };
}