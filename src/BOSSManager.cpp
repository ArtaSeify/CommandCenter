#include "BOSSManager.h"
#include "CCBot.h"
#include "CombatSearchResults.h"
#include "BOSS.h"
#include "Tools.h"
#include <thread>

using namespace CC;

BOSSManager::BOSSManager(CCBot & bot)
    : m_bot                     (bot)
    , m_searcher                ()
    , m_currentGameState        ()
    , m_searching               (false)
    , m_searchFinished          (false)
    , m_largestFrameSearched    (0)
    , m_currentUnits            ()
    , m_searchThread            ()
{
    // Initialize all the BOSS internal data
    BOSS::Init("../BOSS/bin/SC2Data.json");
    m_params = BOSS::CombatSearchParameters();
}

void BOSSManager::setParameters(int frameLimit, float timeLimit, bool alwaysMakeWorkers, bool sortActions,
                                const std::vector<std::pair<BOSS::ActionType, int>> & maxActions,
                                const BOSS::BuildOrderAbilities & openingBuildOrder,
                                const BOSS::ActionSetAbilities & relevantActions)
{
    // set the maxActions
    for (auto & maxActionPair : maxActions)
    {
        m_params.setMaxActions(maxActionPair.first, maxActionPair.second);
    }
    
    // set relevant actions
    m_params.setRelevantActions(relevantActions);

    // heuristic used in search
    m_params.setAlwaysMakeWorkers(alwaysMakeWorkers);

    // the initial build order to follow
    m_params.setOpeningBuildOrder(openingBuildOrder);

    // time limit of the search
    m_params.setSearchTimeLimit(timeLimit);

    // sort moves as we search
    m_params.setSortActions(sortActions);
    
    //int m_threadsForExperiment;
    /*m_params.setExplorationValue(BOSS::FracType(0.15));
    m_params.setChangingRoot(true);
    m_params.setUseMaxValue(true);
    m_params.setNumberOfSimulations(5000000);
    m_params.setSimulationsPerStep(50000); */   

    m_params.setLevel(1);
    m_params.setNumPlayouts(250);

    if (m_largestFrameSearched == 0)
    {
        std::cout << "first search!" << std::endl;
    }

    m_params.setInitialState(m_currentGameState);

    // limit is start frame of the last unit in the build order + framelimit
    std::cout << "setting frame limit as: " << m_largestFrameSearched + frameLimit << std::endl;
    m_params.setFrameTimeLimit(m_largestFrameSearched + frameLimit);
}

void BOSSManager::setCurrentGameState()
{
    //TODO: STATE IS CHANGED IF ANY UNIT DIES.
    // we only set the game state at the very first search
    if (m_currentGameState.getRace() != BOSS::Races::None)
    {
        return;
    }

    setCurrentUnits(m_bot.UnitInfo().getUnits(Players::Self));
    BOSS::GameState state(m_currentUnits, BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Self)),
                        BOSS::FracType(m_bot.GetMinerals()), BOSS::FracType(m_bot.GetGas()),
                        BOSS::NumUnits(m_bot.GetCurrentSupply()), BOSS::NumUnits(m_bot.GetMaxSupply()),
                        BOSS::NumUnits(m_bot.Workers().getNumMineralWorkers()), BOSS::NumUnits(m_bot.Workers().getNumGasWorkers()),
                        BOSS::NumUnits(m_bot.Workers().getNumBuilderWorkers()), BOSS::TimeType(m_bot.GetCurrentFrame()),
                        BOSS::NumUnits(m_bot.Workers().getNumRefineries()), BOSS::NumUnits(m_bot.Workers().getNumDepots()));

    m_currentGameState = state;

    setOpeningBuildOrder();
}

// need to transform a vector of CC::Units to BOSS::Units
void BOSSManager::setCurrentUnits(const std::vector<Unit> & CCUnits)
{
    std::vector<std::pair<Unit, int>> unitsBeingTrained;        // <Unit being trained, builderID>
    std::vector<Unit> unitsBeingConstructed;
    std::vector<Unit> unitsFinished;

    for (auto it = CCUnits.begin(); it != CCUnits.end(); ++it)
    {
        if (!it->isCompleted() && it->isTraining())
        {
            BOT_ASSERT(false, "not completed but training something??");
        }

        if (it->isBeingConstructed() && it->isTraining())
        {
            BOT_ASSERT(false, "being constructed and training at the same time???");
        }

        // unit is completed
        if (it->isCompleted())
        {
            unitsFinished.push_back(*it);
        }

        // unit is training another unit
        if (it->isTraining())
        {
            UnitType type = m_bot.Data(it->getUnitPtr()->orders[0].ability_id);
            if (type.isValid())
            {
                Unit unit(m_bot.Data(it->getUnitPtr()->orders[0].ability_id), m_bot);
                unitsBeingTrained.emplace_back(unit, int(unitsFinished.size() - 1));
            }
        }

        // unit is being constructed
        if (it->isBeingConstructed())
        {
            unitsBeingConstructed.push_back(*it);
        }        
    }

    m_currentUnits.clear();

    // the units that are finished. easiest case
    for (auto it = unitsFinished.begin(); it != unitsFinished.end(); ++it)
    {
        // get the type name from SC2API
        std::string sc2TypeName = it->getType().getName();
        // data required to create BOSS unit
        BOSS::ActionType type = BOSS::ActionTypes::GetActionType(sc2TypeName);
        BOSS::NumUnits id = BOSS::NumUnits(it - unitsFinished.begin());
        BOSS::NumUnits builderID = -1;

        m_currentUnits.push_back(BOSS::Unit(type, id, builderID, 0));
    }

    // the units (buildings) that are being constructed. We need to find out how many frames until the unit
    // is finished. We also need to find the builder and set the appropriate variables for it
    for (auto it = unitsBeingConstructed.begin(); it != unitsBeingConstructed.end(); ++it)
    {
        std::string sc2TypeName = it->getType().getName();
        BOSS::ActionType type = BOSS::ActionTypes::GetActionType(sc2TypeName);
        BOSS::NumUnits id = BOSS::NumUnits(unitsFinished.size() + (it - unitsBeingConstructed.begin()));

        // get the builderID by iterating through all the finished units. If a finished unit
        // is producing something that the tag of what it's producing matches the tag of the building
        // being produced, that is the builder of this unit
        BOSS::NumUnits builderID = -1;
        for (auto unit = unitsFinished.begin(); unit != unitsFinished.end(); ++unit)
        {
            if (unit->isTraining() && unit->getUnitPtr()->orders[0].target_unit_tag == it->getID())
            {
                builderID = BOSS::NumUnits(unit - unitsFinished.begin());
            }
        }

        // get the frame we started building this unit. Don't have to worry about chronoboost
        // because you can't chronoboost buildings
        int startFrame = (int)std::floor(m_bot.GetCurrentFrame() - 
                                (m_bot.Data(*it).buildTime * it->getBuildPercentage()));

        BOSS::Unit unit(type, id, builderID, startFrame);

        unit.setTimeUntilBuilt(BOSS::TimeType(type.buildTime() - (m_bot.GetCurrentFrame() - startFrame)));
        unit.setTimeUntilFree(BOSS::TimeType(unit.getTimeUntilBuilt()));

        if (builderID != -1)
        {
            BOSS::Unit builder_unit = m_currentUnits[builderID];
            builder_unit.setTimeUntilFree(unit.getTimeUntilBuilt());
        }

        m_currentUnits.push_back(unit);
    }

    // units being trained
    for (auto it = unitsBeingTrained.begin(); it != unitsBeingTrained.end(); ++it)
    {
        CC::Unit & ccunit = it->first;

        std::string sc2TypeName = ccunit.getType().getName();

        BOSS::ActionType type = BOSS::ActionTypes::GetActionType(sc2TypeName);
        BOSS::NumUnits id = BOSS::NumUnits(unitsFinished.size() + unitsBeingConstructed.size() + (it - unitsBeingTrained.begin()));

        BOSS::NumUnits builderID = it->second;

        //TODO: TERRAN BUILDINGS WITH REACTOR CAN PRODUCE TWO UNITS AT THE SAME TIME 
        //TODO: TAKE INTO ACCOUNT CHRONOBOOST
        int startFrame = 0;
        auto & builderOrders = unitsFinished[it->second].getUnitPtr()->orders;
        if (m_bot.Data(builderOrders[0].ability_id) == ccunit.getType())
        {
            startFrame = (int)std::floor(m_bot.GetCurrentFrame() -
                                builderOrders[0].progress * m_bot.Data(ccunit).buildTime);
        }

        else
        {
            BOT_ASSERT(false, "Error in ordering of units!!!");
        }
        BOSS::Unit unit(type, id, builderID, startFrame);

        unit.setTimeUntilBuilt(BOSS::TimeType(type.buildTime() - (m_bot.GetCurrentFrame() - startFrame)));
        unit.setTimeUntilFree(BOSS::TimeType(unit.getTimeUntilBuilt()));

        if (builderID != -1)
        {
            BOSS::Unit builder_unit = m_currentUnits[builderID];
            builder_unit.setTimeUntilFree(unit.getTimeUntilBuilt());
        }

        m_currentUnits.push_back(unit);
    }
}

void BOSSManager::startSearch()
{
    m_searching = true;

    //std::thread ([this] {threadSearch(); });
    m_searchThread = std::thread(&BOSSManager::threadSearch, this);
}

void BOSSManager::finishSearch()
{
    m_searcher->finishSearch();
}

void BOSSManager::threadSearch()
{
    //m_searcher = BOSS::CombatSearch_Integral(m_params);
    m_searcher = std::make_unique<BOSS::NMCS>(m_params);
    
    m_searcher->search();

    searchFinished();

    // free the class thread variable, indicating we are done
    m_searchThread.detach();
}

void BOSSManager::searchFinished()
{
    m_results = m_searcher->getResults();

    if (m_results.solved)
    {
        std::cout << "search solved!" << std::endl;
    }
    if (m_results.timedOut)
    {
        std::cout << "search timed out!" << std::endl;
    }

    // update GameState with the new build order we found
    for (auto & actionTargetPair : m_results.buildOrder)
    {        
        auto & action = actionTargetPair.first;
        auto & target = actionTargetPair.second;

        // do the ability
        if (action.isAbility())
        {
            m_currentGameState.doAbility(action, target.targetID);
        }

        // perform the action
        else
        {
            m_currentGameState.doAction(action);
        }
    }

    // the biggest frame we've searched to is the start time of the last unit
    // in the build order
    m_largestFrameSearched = m_currentGameState.getCurrentFrame();

    m_searcher->printResults();
    //std::cout << "\nSearched " << m_results.nodesExpanded << " nodes in " << m_results.timeElapsed << "ms @ " << (1000.0*m_results.nodesExpanded / m_results.timeElapsed) << " nodes/sec\n\n";

    m_searching = false;
    m_searchFinished = true;
}

void BOSSManager::setOpeningBuildOrder()
{
    BuildOrder inputBuildOrder = m_bot.Strategy().getOpeningBookBuildOrder();
    BOSS::BuildOrderAbilities BOSSBuildOrder;

    // create a BOSS build order from the CC build order
    for (int index = 0; index < inputBuildOrder.size(); ++index)
    {
        BOSS::ActionType BOSSType;
        if (inputBuildOrder[index].isAbility() && m_bot.GetPlayerRace(Players::Self) == CCRace::Protoss)
        {
            BOSSType = BOSS::ActionTypes::GetSpecialAction(BOSS::Races::Protoss);
        }
        else
        {
            BOSSType = BOSS::ActionTypes::GetActionType(inputBuildOrder[index].getUnitType().getName());
        }
        
        // TODO: CONSIDER ABILITIES
        if (BOSSType.isAbility())
        {
            continue;
        }

        BOSSBuildOrder.add(BOSSType);
    }

    // do the build order
    BOSS::Tools::DoBuildOrder(m_currentGameState, BOSSBuildOrder);
    m_largestFrameSearched = m_currentGameState.getCurrentFrame();
}

const BOSS::BuildOrderAbilities & BOSSManager::getBuildOrder()
{
    return m_results.buildOrder;
}

BOSS::RaceID BOSSManager::getBOSSPlayerRace() const
{
    return BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Self));
}

int BOSSManager::numSupplyProviders() const
{
    return m_currentGameState.getNumTotal(BOSS::ActionTypes::GetSupplyProvider(getBOSSPlayerRace()));
}

