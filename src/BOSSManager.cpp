#include "BOSSManager.h"
#include "CCBot.h"

BOSSManager::BOSSManager(CCBot & bot)
    : m_bot              (bot)
    , m_currentGameState (BOSS::GameState())
{
    
}

void BOSSManager::setParameters(int frameLimit, float timeLimit, bool alwaysMakeWorkers,
                                const std::vector<std::pair<BOSS::ActionType, int>> & maxActions,
                                const BOSS::BuildOrderAbilities & openingBuildOrder,
                                const BOSS::ActionSetAbilities & relevantActions) const
{
    BOSS::CombatSearchParameters params;

    // set the maxActions
    for (auto & maxActionPair : maxActions)
    {
        params.setMaxActions(maxActionPair.first, maxActionPair.second);
    }
    
    // set relevant actions
    params.setRelevantActions(relevantActions);

    // heuristic used in search
    params.setAlwaysMakeWorkers(alwaysMakeWorkers);

    // the initial build order to follow
    params.setOpeningBuildOrder(openingBuildOrder);

    // time limit of the search
    params.setSearchTimeLimit(timeLimit);

    // search to current_frame + frameLimit
    params.setFrameTimeLimit(frameLimit);
    
    // current state of the game
    //setInitialState(const GameState & s);
}

void BOSSManager::setCurrentGameState()
{
    setCurrentUnits(m_bot.UnitInfo().getUnits(Players::Self));

    BOSS::GameState state(m_currentUnits);
    
    state.setMinerals(m_bot.GetMinerals());
    state.setGas(m_bot.GetGas());

    m_currentGameState = state;
}

void BOSSManager::setCurrentUnits(const std::vector<CCUnit::Unit> & CCUnits)
{

}