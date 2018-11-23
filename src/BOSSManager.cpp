#include "BOSSManager.h"
#include "CCBot.h"

using namespace CC;

BOSSManager::BOSSManager(CCBot & bot)
    : m_bot              (bot)
    , m_currentGameState (BOSS::GameState())
{
    
}

void BOSSManager::setParameters(int frameLimit, float timeLimit, bool alwaysMakeWorkers,
                                const std::vector<std::pair<BOSS::ActionType, int>> & maxActions,
                                const BOSS::BuildOrderAbilities & openingBuildOrder,
                                const BOSS::ActionSetAbilities & relevantActions)
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
    
    setCurrentGameState();
    // current state of the game
    params.setInitialState(m_currentGameState);
}

void BOSSManager::setCurrentGameState()
{
    setCurrentUnits(m_bot.UnitInfo().getUnits(Players::Self));

    BOSS::GameState state(m_currentUnits, m_bot.GetPlayerRace(Players::Self), 
                        m_bot.GetMinerals(), m_bot.GetGas(), 
                        m_bot.GetCurrentSupply(), m_bot.GetMaxSupply(),
                        m_bot.Workers().getNumMineralWorkers(), m_bot.Workers().getNumGasWorkers(),
                        m_bot.Workers().getNumBuilderWorkers(), m_bot.GetCurrentFrame(),
                        m_bot.Workers().getNumRefineries(), m_bot.Workers().getNumDepots());

    m_currentGameState = state;
}

void BOSSManager::setCurrentUnits(const std::vector<Unit> & CCUnits)
{
    // need to transform a vector of CC::Units to BOSS::Units
    //::Unit(ActionType type, NumUnits id, NumUnits builderID, TimeType frameStarted)
    std::vector<std::pair<Unit, int>> unitsBeingTrained;        // <Unit being trained, builderID>
    std::vector<Unit> unitsBeingConstructed;
    std::vector<Unit> unitsFinished;

    for (auto it = CCUnits.begin(); it != CCUnits.end(); ++it)
    {
        if (!it->isCompleted() && it->isTraining())
        {
            std::cout << "WTF??" << std::endl;
            throw;
        }

        // unit is completed
        if (it->isCompleted())
        {
            unitsFinished.push_back(*it);
        }

        // unit is training another unit
        if (it->isTraining())
        {
            Unit unit(m_bot.Data(it->getUnitPtr()->orders[0].ability_id), m_bot);
            unitsBeingTrained.emplace_back(unit, unitsFinished.size() - 1);
        }

        if (it->isBeingConstructed())
        {
            it->
        }

        
        
        
    }

    for (auto it = CCUnits.begin(); it != CCUnits.end(); ++it)
    {
        //Unit(ActionType type, NumUnits id, NumUnits builderID, TimeType frameStarted);
        std::string sc2TypeName = it->getType().getName();
        sc2TypeName = sc2TypeName.substr(sc2TypeName.find("_") + 1, std::string::npos);

        BOSS::ActionType type = BOSS::ActionTypes::GetActionType(sc2TypeName);
        BOSS::NumUnits id = it - CCUnits.begin();
        BOSS::NumUnits builderID = -1;
    }
}