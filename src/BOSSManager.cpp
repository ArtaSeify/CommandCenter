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
    , m_searchState             (SearchState::Free)
    , m_largestFrameSearched    (0)
    , m_currentUnits            ()
    , m_searchThread            ()
    , m_unitStartTimes          ()
{
    // Initialize all the BOSS internal data
    BOSS::Init("../bin/SC2Data.json");
    m_params = BOSS::CombatSearchParameters();
}

void BOSSManager::onStart()
{
    std::vector<std::string> relevantActionsNames =
    { "ChronoBoost", "Probe", "Pylon", "Nexus", "Assimilator", "Gateway", "CyberneticsCore", "Stalker",
        "Zealot", "Colossus", "Forge", "FleetBeacon", "TwilightCouncil", "Stargate", "TemplarArchive",
        "DarkShrine", "RoboticsBay", "RoboticsFacility", "ZealotWarped", "Zealot", "StalkerWarped", "Stalker",
        "DarkTemplarWarped", "DarkTemplar", "Carrier", "VoidRay", "Immortal", "Probe", "AdeptWarped",
        "Adept", "Tempest"};
    //, "WarpGateResearch" 
    BOSS::ActionSetAbilities relevantActions;
    for (std::string & actionName : relevantActionsNames)
    {
        relevantActions.add(BOSS::ActionTypes::GetActionType(actionName));
    }

    // how many times each action is allowed
    std::vector<std::pair<BOSS::ActionType, int>> maxActions;
    maxActions.push_back(std::make_pair(BOSS::ActionTypes::GetActionType("CyberneticsCore"), 1));
    maxActions.push_back(std::make_pair(BOSS::ActionTypes::GetActionType("FleetBeacon"), 1));
    maxActions.push_back(std::make_pair(BOSS::ActionTypes::GetActionType("TwilightCouncil"), 1));
    maxActions.push_back(std::make_pair(BOSS::ActionTypes::GetActionType("TemplarArchive"), 1));
    maxActions.push_back(std::make_pair(BOSS::ActionTypes::GetActionType("DarkShrine"), 1));
    maxActions.push_back(std::make_pair(BOSS::ActionTypes::GetActionType("RoboticsBay"), 1));

    bool sortActions = false;

    // set the maxActions
    for (auto & maxActionPair : maxActions)
    {
        m_params.setMaxActions(maxActionPair.first, maxActionPair.second);
    }

    // set relevant actions
    m_params.setRelevantActions(relevantActions);

    // heuristic used in search
    m_params.setAlwaysMakeWorkers(true);

    // time limit of the search
    m_params.setSearchTimeLimit(10000000);

    // sort moves as we search
    m_params.setSortActions(sortActions);

    //int m_threadsForExperiment;
    m_params.setExplorationValue(BOSS::FracType(0.15));
    m_params.setChangingRoot(true);
    m_params.setUseMaxValue(true);
    m_params.setNumberOfSimulations(500000);
    m_params.setSimulationsPerStep(200);
}

void BOSSManager::onFrame(SearchMessage message)
{
    printDebugInfo();
    // supply maxed, no reason to search
    if (m_currentGameState.getCurrentSupply() == 200)
    {
        return;
    }

    if (message == SearchMessage::QueueEmpty || message == SearchMessage::UnitDied)
    {
        finishSearch(message);
    }

    if (m_searchState == SearchState::Free)
    {
        // supply maxed, no reason to search
        if (m_currentGameState.getCurrentSupply() == 200)
        {
            return;
        }

        bool reset = false;
        if (message == SearchMessage::UnitDied)
        {
            reset = true;
        }
        startSearch(reset);
    }
}

void BOSSManager::setParameters(bool reset)
{
    setCurrentGameState(reset);

    // limit is start frame of the last unit in the build order + framelimit
    const int frameLimit = 6720;
    //std::cout << "setting frame limit as: " << m_largestFrameSearched + frameLimit << std::endl;
    m_params.setFrameTimeLimit(m_largestFrameSearched + frameLimit);
}

void BOSSManager::setCurrentGameState(bool reset)
{
    //TODO: STATE IS CHANGED IF ANY UNIT DIES.
    // we only set the game state at the very first search
    if (m_currentGameState.getRace() != BOSS::Races::None && !reset)
    {
        m_params.setInitialState(m_currentGameState);

        // the enemy data
        setEnemyUnits();

        //m_params.setEnemyUnits(m_enemyUnits);
        //m_params.setEnemyRace(BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Enemy)));
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
    // set the initial build order 
    if (m_currentGameState.getCurrentFrame() <= 1)
    {
        setOpeningBuildOrder();
    }
    m_params.setInitialState(m_currentGameState);	
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

        BOSS::Unit unit(type, id, builderID, 0);
        unit.setEnergy(it->getEnergy());
        m_currentUnits.push_back(unit);
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

void BOSSManager::setEnemyUnits()
{
    // enemy units
    m_enemyUnits.clear();
    m_enemyUnits = std::vector<int>(BOSS::ActionTypes::GetAllActionTypes().size(), 0);

    std::cout << "enemy units:" << std::endl;
    auto enemyUnits = m_bot.UnitInfo().getUnits(Players::Enemy);
    for (auto& enemyUnit : enemyUnits)
    {
        m_enemyUnits[BOSS::ActionTypes::GetActionType(enemyUnit.getType().getName()).getID()]++;
        std::cout << enemyUnit.getType().getName() << std::endl;
    }
}

void BOSSManager::startSearch(bool reset)
{
    setParameters(reset);

    m_searchState = SearchState::Searching;
    m_searchThread = std::thread(&BOSSManager::threadSearch, this);
    m_searchThread.detach();
}

void BOSSManager::finishSearch(SearchMessage message)
{
    if (m_searchState != SearchState::Searching)
    {
        return;
    }

    m_searcher->finishSearch();
    m_searchState = SearchState::ExitSearch;

    // WAIT
    while (m_searchState != SearchState::Free);

    if (message == SearchMessage::QueueEmpty)
    {
        queueEmpty();
    }
    else if (message == SearchMessage::UnitDied)
    {
        unitDied();
    }
}

void BOSSManager::threadSearch()
{
    while (m_searchState == SearchState::Searching)
    {
        m_searcher = std::unique_ptr<BOSS::CombatSearch>(new BOSS::CombatSearch_IntegralMCTS(m_params));        
        m_searcher->search();
        std::cout << "search finished!" << std::endl;
        m_searchResults.push_back(m_searcher->getResults());
    }

    // free the class thread variable, indicating we are done
    m_searchState = SearchState::Free;
}

void BOSSManager::getResult()
{
    m_searchState = SearchState::GettingResults;
    m_results = m_searchResults[0];

    for (auto& result : m_searchResults)
    {
        if (result.usefulEval > m_results.usefulEval)
        {
            m_results = result;
        }
    }
}

void BOSSManager::storeUnitStartTime(const BOSS::ActionAbilityPair & action)
{
    int frame = m_currentGameState.getCurrentFrame();
    double time = (frame / 22.4) / 60;
    double minute, second;
    second = modf(time, &minute);
    second *= 60;
    second = (int)std::ceil(second);
    if (second == 60)
    {
        minute++;
        second = 0;
    }

    std::stringstream ss;

    ss << minute << ":";
    if (second / 10 == 0)
    {
        ss << "0" << second;
    }
    else
    {
        ss << second;
    }

    m_unitStartTimes.push_back(std::make_pair(ss.str(), action.first.getName()));
}

void BOSSManager::queueEmpty()
{
    getResult();

    m_unitStartTimes.clear();
    // update GameState with the new build order we found
    for (auto& action : m_results.usefulBuildOrder)
    {
        if (action.first.isAbility())
        {
            m_currentGameState.doAbility(action.first, action.second.targetID);
        }
        else
        {
            m_currentGameState.doAction(action.first);
        }

        storeUnitStartTime(action);
    }

    // the biggest frame we've searched to is the start time of the last unit
    // in the build order
    m_largestFrameSearched = m_currentGameState.getCurrentFrame();

    //m_searcher->printResults();
    //std::cout << "\nSearched " << m_results.nodesExpanded << " nodes in " << m_results.timeElapsed << "ms @ " << (1000.0*m_results.nodesExpanded / m_results.timeElapsed) << " nodes/sec\n\n";

    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::unitDied()
{
    m_searchState = SearchState::GettingResults;
    m_results = m_searchResults[0];
    double avgSearchTime = 0;

    for (auto& result : m_searchResults)
    {
        avgSearchTime += result.timeElapsed;
        if (result.usefulEval > m_results.usefulEval)
        {
            m_results = result;
        }
    }
    avgSearchTime /= (m_searchResults.size() * 1000);

    int gameFrame = m_bot.GetCurrentFrame();
    int searchFrame = m_currentGameState.getCurrentFrame();
    // we have enough time to start a new search and have it finish before our current
    // build order is done
    if (searchFrame - gameFrame >= avgSearchTime * m_bot.GetFramesPerSecond())
    {
        std::cout << "have time to restart!" << std::endl;
        m_searchResults.clear();
        m_searchState = SearchState::Free;
        return;
    }

    m_unitStartTimes.clear();
    // update GameState with the new build order we found
    for (auto& action : m_results.usefulBuildOrder)
    {
        if (action.first.isAbility())
        {
            m_currentGameState.doAbility(action.first, action.second.targetID);
        }
        else
        {
            m_currentGameState.doAction(action.first);
        }

        const int frame = m_currentGameState.getCurrentFrame();
        double time = (frame / 22.4) / 60;
        double minute, second;
        second = modf(time, &minute);
        second *= 60;
        second = (int)std::ceil(second);
        if (second == 60)
        {
            minute++;
            second = 0;
        }

        std::stringstream ss;

        ss << minute << ":";
        if (second / 10 == 0)
        {
            ss << "0" << second;
        }
        else
        {
            ss << second;
        }

        m_unitStartTimes.push_back(std::make_pair(ss.str(), action.first.getName()));

        std::cout << "did action in build order!" << std::endl;
        // only go to the point where the build order will finish by the time the new search finishes
        if (frame - gameFrame >= avgSearchTime * m_bot.GetFramesPerSecond())
        {
            break;
        }
    }

    std::cout << "current supply: " << m_currentGameState.getCurrentSupply() << std::endl;

    // the biggest frame we've searched to is the start time of the last unit
    // in the build order
    m_largestFrameSearched = m_currentGameState.getCurrentFrame();

    //m_searcher->printResults();
    //std::cout << "\nSearched " << m_results.nodesExpanded << " nodes in " << m_results.timeElapsed << "ms @ " << (1000.0*m_results.nodesExpanded / m_results.timeElapsed) << " nodes/sec\n\n";

    m_searchResults.clear();
    m_searchState = SearchState::Free;
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
            AbilityAction abilityInfo = inputBuildOrder[index].getAbility().second;
            BOSSType = BOSS::ActionTypes::GetSpecialAction(BOSS::Races::Protoss);
            BOSS::AbilityAction ability(BOSSType, 0, 0, 0, BOSS::ActionTypes::GetActionType(abilityInfo.target_type.getName()), 
                        BOSS::ActionTypes::GetActionType(abilityInfo.targetProduction_name));
            BOSSBuildOrder.add(BOSSType, ability);
        }
        else
        {
            BOSSType = BOSS::ActionTypes::GetActionType(inputBuildOrder[index].getName());
            BOSSBuildOrder.add(BOSSType);
        }
    }

    // do the build order
    BOSS::Tools::DoBuildOrder(m_currentGameState, BOSSBuildOrder);
    m_largestFrameSearched = m_currentGameState.getCurrentFrame();
}

void BOSSManager::printDebugInfo() const
{
    std::stringstream ss;
    ss << "BOSS Information: ";
    if (m_searchState == SearchState::Searching)
    {
        ss << "Searching";
    }
    else if (m_searchState == SearchState::ExitSearch)
    {
        ss << "Exitting Search";
    }
    else if (m_searchState == SearchState::Free)
    {
        ss << "Free";
    }
    else if (m_searchState == SearchState::GettingResults)
    {
        ss << "Getting Results";
    }
    
    ss << "\n\n";

    if (m_currentGameState.getCurrentFrame() > 1)
    {
        for (auto& unit : m_unitStartTimes)
        {
            ss << unit.first << " " << unit.second << "\n";
        }

        ss << "\nNodes visited: " << m_results.nodeVisits << "\n";
        ss << "Nodes expanded: " << m_results.nodesExpanded << "\n";
        ss << "Search time: " << m_results.timeElapsed << "\n";
    }

    if (m_searcher && m_searchState != SearchState::Free)
    {
        BOSS::Timer t = m_searcher->getResults().searchTimer;
        ss << "\nCurrent search\n\n";
        ss << "Nodes visited: " << m_searcher->getResults().nodeVisits << "\n";
        ss << "Nodes expanded: " << m_searcher->getResults().nodesExpanded << "\n";
        ss << "Search time: " << t.getElapsedTimeInSec() << "\n";
    }

    if (m_searchResults.size() > 0)
    {
        double avgTime = 0;
        BOSS::uint8 nodesExpanded = 0;
        BOSS::uint8 nodesVisited = 0;
        for (auto& result : m_searchResults)
        {
            avgTime += result.timeElapsed;
            nodesExpanded += result.nodesExpanded;
            nodesVisited += result.nodeVisits;
        }
        avgTime /= (m_searchResults.size() * 1000);
        nodesExpanded /= m_searchResults.size();
        nodesVisited /= m_searchResults.size();

        ss << "\nNext build order search stats\n\n";
        ss << "Searches completed: " << m_searchResults.size() << "\n";
        ss << "Average nodes visited: " << nodesVisited << "\n";
        ss << "Average nodes expanded: " << nodesExpanded << "\n";
        ss << "Average search length: " << avgTime << "\n";
    }

    m_bot.Map().drawTextScreen(0.72f, 0.05f, ss.str(), CCColor(255, 255, 0));
}

const BOSS::BuildOrderAbilities & BOSSManager::getBuildOrder()
{
    return m_results.usefulBuildOrder;
}
