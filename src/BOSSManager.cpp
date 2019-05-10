#include "BOSSManager.h"
#include "CCBot.h"
#include "CombatSearchResults.h"
#include "BOSS.h"
#include "Tools.h"
#include <thread>

using namespace CC;

BOSSManager::BOSSManager(CCBot & bot, BuildingManager & buildingManager)
    : m_bot                     (bot)
    , m_queue                   (bot)
    , m_searcher                ()
    , m_buildingManager         (buildingManager)
    , m_enemyUnits              ()
    , m_futureGameState         ()
    , m_currentBuildOrder       ()
    , m_currentGameState        ()
    , m_previousBuildOrder      ()
    , m_searchState             (SearchState::Free)
    , m_searchThread            ()
    , m_unitInfo                ()
    , m_fastReaction            (false)
    , m_deadUnit                (false)
    , m_needFastReaction        (false)
{
    // Initialize all the BOSS internal data
    BOSS::Init("../bin/SC2Data.json");
    m_params = BOSS::CombatSearchParameters();
}

void BOSSManager::setBuildOrder(const BuildOrder& buildOrder)
{
    m_queue.clearAll();

    for (size_t i(0); i < buildOrder.size(); ++i)
    {
        m_queue.queueAsLowestPriority(buildOrder[i], true);
    }
}

void BOSSManager::onStart()
{
    setBuildOrder(m_bot.Strategy().getOpeningBookBuildOrder());

    std::vector<std::string> relevantActionsNames =
    { "ChronoBoost", "Probe", "Pylon", "Nexus", "Assimilator", "Gateway", "CyberneticsCore", "Stalker",
        "Zealot", "Colossus", "FleetBeacon", "TwilightCouncil", "Stargate", "TemplarArchive",
        "DarkShrine", "RoboticsBay", "RoboticsFacility", "DarkTemplar", "Carrier", "VoidRay", "Immortal", "Probe", "Adept", "Tempest" };
    //, "WarpGateResearch" 
    /*std::vector<std::string> relevantActionsNames =
    { "ChronoBoost", "Probe", "Pylon", "Nexus", "Assimilator", "Gateway", "CyberneticsCore", "Stalker",
        "Zealot", "Forge", "FleetBeacon", "Stargate", "Carrier", "VoidRay", "Adept", "Tempest" };*/
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
    m_params.setNumberOfSimulations(10000000);
    m_params.setSimulationsPerStep(1000);
}

void BOSSManager::onFrame()
{
    const auto deadUnits = m_bot.UnitInfo().getUnitsDied(Players::Self);

    // supply maxed and no units died, no reason to search.
    // sometimes there is nothing in Q but the supplies don't match. we start a new search in that case
    if (m_futureGameState.getCurrentSupply() == 200 && deadUnits.size() == 0 && !(m_queue.isEmpty() && m_bot.GetCurrentSupply() < 200))
    {
        return;
    }

    // queue is empty
    if (m_queue.isEmpty() && m_bot.GetCurrentSupply() < 200)
    {
        finishSearch();
        queueEmpty();
        addToQueue(m_currentBuildOrder);
    }
    // one of our units died
    else if (deadUnits.size() > 0)
    {
        unitsDied(deadUnits);
    }
    // saw a new enemy unit
    else if (setEnemyUnits())
    {
        newEnemyUnit();
    } 

    if (m_searchState == SearchState::Free)
    {
        // supply maxed and no units died, no reason to search
        if (m_futureGameState.getCurrentSupply() == 200 && deadUnits.size() == 0 && !(m_queue.isEmpty() && m_bot.GetCurrentSupply() < 200))
        {
            return;
        }

        setParameters(false);
        startSearch();
    }    

    printDebugInfo();

    if (m_queue.isEmpty() && m_bot.GetCurrentSupply() < 200)
    {
        system("pause");
    }
}

void BOSSManager::addToQueue(const BOSS::BuildOrderAbilities & buildOrder)
{
    for (auto& actionTargetPair : buildOrder)
    {
        auto& actionType = actionTargetPair.first;
        auto& target = actionTargetPair.second;

        if (actionType.isAbility())
        {
            AbilityAction abilityInfo;
            abilityInfo.target_type = UnitType::GetUnitTypeFromName(target.targetType.getName(), m_bot);
            MetaType targetProd = MetaType(target.targetProductionType.getName(), m_bot);
            if (targetProd.isUpgrade())
            {
                abilityInfo.targetProduction_ability = targetProd.getAbility().first;
            }
            else if (targetProd.isUnit())
            {
                abilityInfo.targetProduction_ability = m_bot.Data(targetProd.getUnitType()).buildAbility;
            }
            abilityInfo.targetProduction_name = targetProd.getName();
            MetaType action("ChronoBoost", abilityInfo, m_bot);
            m_queue.queueAsLowestPriority(action, true);
        }
        else
        {
            MetaType action(actionType.getName(), m_bot);
            m_queue.queueAsLowestPriority(action, true);
        }
    }
}

void BOSSManager::setParameters(bool reset)
{
    setCurrentGameState(reset);
    m_params.setInitialState(m_futureGameState);

    if (m_needFastReaction)
    {
        m_params.setFrameTimeLimit(m_futureGameState.getCurrentFrame() + frameLimitFastReaction);
        m_needFastReaction = false;
    }
    else
    {
        m_params.setFrameTimeLimit(m_futureGameState.getCurrentFrame() + frameLimit);
    }

    m_params.setEnemyUnits(m_enemyUnits);
    m_params.setEnemyRace(BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Enemy)));
}

void BOSSManager::setCurrentGameState(bool reset)
{
    // if we don't need to reset the game state, just use the previous one we got from BOSS
    if (m_futureGameState.getRace() != BOSS::Races::None && !reset)
    {
        return;
    }

    // set a new game state using the actual game state
    BOSS::GameState state(getCurrentUnits(), BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Self)),
        BOSS::FracType(m_bot.GetMinerals()), BOSS::FracType(m_bot.GetGas()),
        BOSS::NumUnits(m_bot.GetCurrentSupply()), BOSS::NumUnits(m_bot.GetMaxSupply()),
        BOSS::NumUnits(m_bot.Workers().getNumMineralWorkers() + m_bot.Workers().getNumBuilderWorkers()), BOSS::NumUnits(m_bot.Workers().getNumGasWorkers()),
        BOSS::NumUnits(m_bot.Workers().getNumWorkers() - (m_bot.Workers().getNumMineralWorkers() + m_bot.Workers().getNumBuilderWorkers() + m_bot.Workers().getNumGasWorkers())),
        BOSS::TimeType(m_bot.GetCurrentFrame()), BOSS::NumUnits(m_bot.Workers().getNumRefineries()), BOSS::NumUnits(m_bot.Workers().getNumDepots()));

    //std::cout << "num workers inside BOSS state: " << state.getNumTotal(BOSS::ActionTypes::GetWorker(BOSS::Races::Protoss)) << std::endl;
    //std::cout << "num workers in actual game: " << m_bot.Workers().getNumWorkers() << std::endl;

    m_currentGameState = state;
    m_futureGameState = state;
    
    doBuildingsInQueue(m_futureGameState);

    // do the initial build order so the state is in the proper spot
    if (m_futureGameState.getCurrentFrame() == 1)
    {
        doBuildOrder(m_bot.Strategy().getOpeningBookBuildOrder());
    }
    else
    {
        fixBuildOrder(m_futureGameState, m_currentBuildOrder, int(m_currentBuildOrder.size() - m_queue.size()), false);
    }
}

std::vector<BOSS::Unit> BOSSManager::getCurrentUnits()
{
    std::vector<std::pair<Unit, int>> unitsBeingTrained;        // <Unit being trained, builderID>
    std::vector<Unit> unitsBeingConstructed;
    std::vector<Unit> unitsFinished;
    std::vector<BOSS::Unit> currentUnits;

    for (const Unit & CCUnit : m_bot.UnitInfo().getUnits(Players::Self))
    {
        if (!CCUnit.isCompleted() && CCUnit.isTraining())
        {
            BOT_ASSERT(false, "not completed but training something??");
        }

        if (CCUnit.isBeingConstructed() && CCUnit.isTraining())
        {
            BOT_ASSERT(false, "being constructed and training at the same time???");
        }

        // unit is completed
        if (CCUnit.isCompleted())
        {
            unitsFinished.push_back(CCUnit);
        }

        // unit is training another unit
        if (CCUnit.isTraining())
        {
            UnitType type = m_bot.Data(CCUnit.getUnitPtr()->orders[0].ability_id);
            if (type.isValid())
            {
                Unit unit(m_bot.Data(CCUnit.getUnitPtr()->orders[0].ability_id), m_bot);
                unitsBeingTrained.push_back(std::pair<Unit, int>(unit, int(unitsFinished.size() - 1)));
                std::cout << m_bot.Data(CCUnit.getUnitPtr()->orders[0].ability_id).getName() << std::endl;
            }
        }

        // unit is being constructed
        if (CCUnit.isBeingConstructed())
        {
            unitsBeingConstructed.push_back(CCUnit);
        }        
    }

    /*std::cout << "before" << std::endl;
    for (int index = 0; index < m_futureGameState.getNumUnits(); ++index)
    {
        const BOSS::Unit& unit = static_cast<const BOSS::GameState>(m_futureGameState).getUnit(index);
        std::cout << "units.push_back(Unit(ActionTypes::GetActionType(\"" << unit.getType().getName() << "\"), " << unit.getID() << ", " << (unit.getTimeUntilBuilt() == 0 ? -1 : unit.getBuilderID()) << ", " << unit.getStartFrame() << "));" << std::endl;
    }*/

    currentUnits.clear();
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

        currentUnits.push_back(unit);
    }

    // if there are any workers inside geysers, they are not inside of Observation(),
    // so we account for them separately
    int numWorkers = 0;
    for (auto it = currentUnits.begin(); it != currentUnits.end(); ++it)
    {
        if (it->getType() == BOSS::ActionTypes::GetWorker(BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Self))))
        {
            ++numWorkers;
        }
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
            BOSS::Unit & builder_unit = currentUnits[builderID];
            builder_unit.setTimeUntilFree(unit.getTimeUntilBuilt());
            builder_unit.setBuildType(unit.getType());
            currentUnits[builderID].setBuildID(id);
        }

        currentUnits.push_back(unit);
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

            /*std::cout << "current frame: " << m_bot.GetCurrentFrame() << std::endl;
            std::cout << "order progress: " << builderOrders[0].progress << std::endl;
            std::cout << "build time: " << m_bot.Data(ccunit).buildTime << std::endl;

            std::cout << "all orders" << std::endl;
            for (auto& order : unitsFinished[it->second].getUnitPtr()->orders)
            {
                std::cout << order.ability_id << ", " << order.progress << std::endl;
            }*/
        }

        else
        {
            BOT_ASSERT(false, "Error in order of units!!!");
        }
        BOSS::Unit unit(type, id, builderID, startFrame);

        unit.setTimeUntilBuilt(BOSS::TimeType(type.buildTime() - (m_bot.GetCurrentFrame() - startFrame)));
        unit.setTimeUntilFree(BOSS::TimeType(unit.getTimeUntilBuilt()));

        if (builderID != -1)
        {
            currentUnits[builderID].setTimeUntilFree(unit.getTimeUntilBuilt());
            // set the type of unit the builder is building. This is so we can chronoboost the proper targets
            currentUnits[builderID].setBuildType(BOSS::ActionTypes::GetActionType(ccunit.getType().getName()));
            currentUnits[builderID].setBuildID(id);
        }

        //std::cout << unit.toString() << std::endl;

        currentUnits.push_back(unit);
    }

    // add missing workers
    int missingWorkers = m_bot.Workers().getNumWorkers() - numWorkers;
    int extraWorkersAdded = 0;
    while (missingWorkers > 0)
    {
        BOSS::Unit unit(BOSS::ActionTypes::GetWorker(BOSS::Races::GetRaceID(m_bot.GetPlayerRaceName(Players::Self))),
                            int(unitsFinished.size() + unitsBeingConstructed.size() + unitsBeingTrained.size()) + extraWorkersAdded,
                            -1, 0);
        currentUnits.push_back(unit);
        ++extraWorkersAdded;
        --missingWorkers;
    }

    /*std::cout << "after" << std::endl;
    for (const auto& unit : m_currentUnits)
    {
        std::cout << "units.push_back(Unit(ActionTypes::GetActionType(\"" << unit.getType().getName() << "\"), " << unit.getID() << ", " << unit.getBuilderID() << ", " << unit.getStartFrame() << "));" << std::endl;
    }*/

    /*for (const auto& unit : currentUnits)
    {
        std::cout << unit.toString() << std::endl;
        std::cout << std::endl;
    }*/

    return currentUnits;
}

bool BOSSManager::setEnemyUnits()
{
    auto enemyUnitTypes = std::vector<int>(BOSS::ActionTypes::GetAllActionTypes().size(), 0);
    auto allEnemyUnits = m_bot.UnitInfo().getUnits(Players::Enemy);

    std::vector<Unit> enemyCombatUnits;
    for (const auto& unit : allEnemyUnits)
    {
        if (unit.getType().isCombatUnit() && !unit.getType().isVariation())
        {
            enemyCombatUnits.push_back(unit);
            //std::cout << "enemy combat unit: " << unit.getType().getName() << std::endl;
        }
    }

    // set the new enemy unit vector
    if (enemyCombatUnits.size() >= enemyArmyBeforeReact)
    {
        for (auto& enemyUnit : enemyCombatUnits)
        {
            const int index = BOSS::ActionTypes::GetActionType(enemyUnit.getType().getName()).getID();
            enemyUnitTypes[index]++;
        }

        if (enemyUnitTypes != m_enemyUnits)
        {
            m_enemyUnits = enemyUnitTypes;

            const auto& unitWeightsBefore = BOSS::Eval::GetUnitWeightVector();
            const auto& unitWeightsNow = BOSS::Eval::CalculateUnitWeightVector(m_futureGameState, m_enemyUnits);

            if (unitWeightsBefore == unitWeightsNow)
            {
                std::cout << "new unit, but weight vector doesn't change" << std::endl;
                return false;
            }

            std::cout << "weight vector changed because of new unit" << std::endl;
            return true;
        }
    }

    return false;
}

void BOSSManager::startSearch()
{
    m_searchState = SearchState::Searching;
    m_searchThread = std::thread(&BOSSManager::threadSearch, this);
    m_searchThread.detach();
}

void BOSSManager::finishSearch()
{
    if (m_searchState != SearchState::Searching)
    {
        return;
    }

    m_searcher->finishSearch();
    m_searchState = SearchState::ExitSearch;

    // WAIT
    while (m_searchState != SearchState::Free);
}

void BOSSManager::threadSearch()
{
    while (m_searchState == SearchState::Searching)
    {
        m_searcher = std::unique_ptr<BOSS::CombatSearch>(new BOSS::CombatSearch_IntegralMCTS(m_params));        
        m_searcher->search();
        m_searchResults.push_back(m_searcher->getResults());
    }

    // free the class thread variable, indicating we are done
    m_searchState = SearchState::Free;
}

void BOSSManager::getResult()
{
    m_previousBuildOrder = m_currentBuildOrder;
    m_searchState = SearchState::GettingResults;
    m_results = m_searchResults[0];

    // get the result with the highest value
    for (auto& result : m_searchResults)
    {
        if (result.usefulEval > m_results.usefulEval)
        {
            m_results = result;
        }
    }
}

void BOSSManager::storeBuildOrderInfo(const BOSS::ActionAbilityPair & action, const BOSS::GameState & state)
{
    int frame = state.getCurrentFrame();
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
    if ((int)second / 10 == 0)
    {
        ss << "0" << second;
    }
    else
    {
        ss << second;
    }
    
    ss << " " << action.first.getName();
    ss << " " << state.getMinerals() << " " << state.getGas();
    ss << "\n";

    m_unitInfo += ss.str();
}

void BOSSManager::queueEmpty()
{
    getResult();
    m_currentBuildOrder = m_results.usefulBuildOrder;
    m_currentGameState = m_futureGameState;

    // a worker or combat unit died. we need to reset the game state
    if (m_deadUnit)
    {
        std::cout << "reseting cause of dead unit" << std::endl;
        setCurrentGameState(true);
        m_deadUnit = false;
    }

    // since the planning times and execution times dont exactly match up,
    // our minerals and gas will be different as well. we set the mineral and
    // gas to be the actual current value so BOSS can make a better plan
    //m_futureGameState.setGas(BOSS::FracType(m_bot.GetGas()));
    //m_futureGameState.setMinerals(BOSS::FracType(m_bot.GetMinerals()));

    m_unitInfo.clear();
    BOSS::GameState state(m_futureGameState);
    BOSS::BuildOrderAbilities buildOrder;
    // update GameState with the new build order we found
    for (auto& action : m_currentBuildOrder)
    {
        if (action.first.isAbility())
        {
            state.doAbility(action.first, action.second.targetID);
            //std::cout << "ability successful!" << std::endl;
        }
        else
        {
            state.doAction(action.first);
        }

        // only add actions up to the frame dictated by frameBuildOrderUse
        if (state.getCurrentFrame() > m_bot.GetCurrentFrame() + frameBuildOrderUse)
        {
            break;
        }

        if (action.first.isAbility())
        {
            //std::cout << "adding ability queue empty!" << std::endl;
            std::cout << "adding chronoboost to queue!" << std::endl;
            m_futureGameState.doAbility(action.first, action.second.targetID);
            //std::cout << "ability successful!" << std::endl;
        }
        else
        {
            std::cout << "adding " << action.first.getName() << " to queue!" << std::endl;
            m_futureGameState.doAction(action.first);
        }

        storeBuildOrderInfo(action, state);
        buildOrder.add(action);
    }

    m_currentBuildOrder = buildOrder;
    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::newEnemyUnitFastReaction(int startingIndex)
{
    getResult();
    double avgSearchTime = 0;
    for (auto& result : m_searchResults)
    {
        avgSearchTime += result.timeElapsed;
    }
    avgSearchTime /= (m_searchResults.size() * 1000);

    int gameFrame = m_bot.GetCurrentFrame();
    int searchFrameTime = (int)std::ceil(avgSearchTime * m_bot.GetFramesPerSecond()) * 2;

    BOT_ASSERT(m_previousBuildOrder.size() >= m_queue.size(), "Previous build order size %i smaller than queue size %i", m_previousBuildOrder.size(), m_queue.size());
    int endIndex = startingIndex;

    // keep going until the frame we do an action is after the frame we estimate a search will finish
    while (endIndex < m_previousBuildOrder.size())
    {
        const auto& action = m_previousBuildOrder[endIndex];
        ++endIndex;

        std::cout << "adding action: " << action.first.getName() << std::endl;

        if (action.first.isAbility())
        {
            m_futureGameState.doAbility(action.first, action.second.targetID);
        }
        else
        {
            m_futureGameState.doAction(action.first);
        }

        if (m_futureGameState.getCurrentFrame() > gameFrame + searchFrameTime)
        {
            break;
        }
    }

    m_unitInfo.clear();
    BOSS::BuildOrderAbilities catchUpBuildOrder;

    // add the actions we have to take from the build order we were following
    for (int index = startingIndex; index < endIndex; ++index)
    {
        catchUpBuildOrder.add(m_previousBuildOrder[index]);
    }
    // the search starts before the current build order finishes
    if (endIndex < m_previousBuildOrder.size())
    {
        // there might be a slight difference between planning and execution frames. If nothing is added,
        // we add one action from the previous build order
        BOT_ASSERT(catchUpBuildOrder.size() > 0, "Size of build order must be greater than 0 %i", catchUpBuildOrder.size());
        if (catchUpBuildOrder.size() == 0)
        {
            catchUpBuildOrder.add(m_previousBuildOrder[int(m_previousBuildOrder.size() - m_queue.size())]);
        }
    }

    else
    {
        fixBuildOrder(m_futureGameState, m_results.usefulBuildOrder, 0, false);

        // update GameState with the new build order we found
        for (auto& action : m_results.usefulBuildOrder)
        {
            std::cout << "adding extra action: " << action.first.getName() << std::endl;
            if (action.first.isAbility())
            {
                m_futureGameState.doAbility(action.first, action.second.targetID);
            }
            else
            {
                m_futureGameState.doAction(action.first);
            }
            catchUpBuildOrder.add(action);
            storeBuildOrderInfo(action, m_futureGameState);

            if (m_futureGameState.getCurrentFrame() > gameFrame + searchFrameTime)
            {
                break;
            }
        }
    }
    //std::cout << "num workers in future game state: " << m_futureGameState.getNumTotal(BOSS::ActionTypes::GetWorker(BOSS::Races::Protoss)) << std::endl;
    //m_futureGameState = state;
    m_currentBuildOrder = catchUpBuildOrder;
    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::newEnemyUnitsMediumReaction()
{
    int gameFrame = m_bot.GetCurrentFrame();
    int finishFrame = gameFrame + frameLimitFastReaction;

    BOT_ASSERT(m_currentBuildOrder.size() >= m_queue.size(), "current build order size %i smaller than queue size %i", m_currentBuildOrder.size(), m_queue.size());
    
    BOSS::BuildOrderAbilities newBuildOrder;
    m_unitInfo.clear();
    // keep going until the frame we do an action is after the reaction frame
    for (int index = int(m_currentBuildOrder.size() - m_queue.size()); index < m_currentBuildOrder.size(); ++index)
    {
        const auto& action = m_currentBuildOrder[index];
        std::cout << "adding action: " << action.first.getName() << std::endl;

        if (action.first.isAbility())
        {
            m_futureGameState.doAbility(action.first, action.second.targetID);
        }
        else
        {
            m_futureGameState.doAction(action.first);
        }
        newBuildOrder.add(action);
        storeBuildOrderInfo(action, m_futureGameState);

        // we are passed the point
        if (m_futureGameState.getCurrentFrame() > finishFrame)
        {
            break;
        }
    }

    // we need extra actions from the build orders we have been searching
    if (m_futureGameState.getCurrentFrame() < finishFrame)
    {
        getResult();
        m_currentBuildOrder = m_results.usefulBuildOrder;
        fixBuildOrder(m_futureGameState, m_currentBuildOrder, 0, false);

        for (auto& action : m_currentBuildOrder)
        {
            std::cout << "adding extra action: " << action.first.getName() << std::endl;
            if (action.first.isAbility())
            {
                m_futureGameState.doAbility(action.first, action.second.targetID);
            }
            else
            {
                m_futureGameState.doAction(action.first);
            }
            newBuildOrder.add(action);
            storeBuildOrderInfo(action, m_futureGameState);

            if (m_futureGameState.getCurrentFrame() > finishFrame)
            {
                break;
            }
        }
    }

    m_currentBuildOrder = newBuildOrder;
    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::newEnemyUnitSlowReaction()
{
    int gameFrame = m_bot.GetCurrentFrame();
    BOT_ASSERT(m_params.getFrameTimeLimit() - m_futureGameState.getCurrentFrame() == frameLimit, "Wrong calculation");
    int halfwayPoint = int(m_futureGameState.getCurrentFrame() - (frameLimit / 2.f));

    // we do nothing if we are past the halfway point
    if (gameFrame > halfwayPoint)
    {
        std::cout << "new enemy unit: past halfway point. Will not react" << std::endl;
        return;
    }

    // if we are before the halfway point, we get rid of all the results we have so far and just start over
    std::cout << "new enemy unit: before halfway point. reacting" << std::endl;

    finishSearch();
    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::newEnemyUnit()
{
    if (m_fastReaction)
    {
        finishSearch();

        int startingIndexBeforeCleanUp = int(m_currentBuildOrder.size() - m_queue.size());
        setCurrentGameState(true);
        std::cout << "reacting fast to new enemy unit" << std::endl;
        newEnemyUnitFastReaction(startingIndexBeforeCleanUp);

        m_queue.clearAll();
        addToQueue(m_currentBuildOrder);
    }
    else
    {
        finishSearch();
        setCurrentGameState(true);

        std::cout << "reacting at medium speed to new enemy unit" << std::endl;
        newEnemyUnitsMediumReaction();

        m_queue.clearAll();
        addToQueue(m_currentBuildOrder);


        /*std::cout << "reacting slow to new enemy unit" << std::endl;
        newEnemyUnitSlowReaction();*/
    }
}

void BOSSManager::unitsDiedFastReaction(const std::vector<Unit>& deadUnits, int startingIndex)
{
    getResult();
    double avgSearchTime = 0;
    for (auto& result : m_searchResults)
    {
        avgSearchTime += result.timeElapsed;
    }
    avgSearchTime /= (m_searchResults.size() * 1000);

    int gameFrame = m_bot.GetCurrentFrame();
    int searchFrameTime = (int)std::ceil(avgSearchTime * m_bot.GetFramesPerSecond()) * 2;

    BOT_ASSERT(m_previousBuildOrder.size() >= m_queue.size(), "Previous build order size %i smaller than queue size %i", m_previousBuildOrder.size(), m_queue.size());
    int endIndex = startingIndex;

    // keep going until the frame we do an action is after the frame we estimate a search will finish
    while (endIndex < m_previousBuildOrder.size())
    {
        const auto& action = m_previousBuildOrder[endIndex];
        ++endIndex;

        std::cout << "adding action: " << action.first.getName() << std::endl;

        if (action.first.isAbility())
        {
            m_futureGameState.doAbility(action.first, action.second.targetID);
        }
        else
        {
            m_futureGameState.doAction(action.first);
        }

        if (m_futureGameState.getCurrentFrame() > gameFrame + searchFrameTime)
        {
            break;
        }
    }

    m_unitInfo.clear();
    BOSS::BuildOrderAbilities catchUpBuildOrder;

    // add the actions we have to take from the build order we were following
    for (int index = startingIndex; index < endIndex; ++index)
    {
        catchUpBuildOrder.add(m_previousBuildOrder[index]);
    }
    // the search starts before the current build order finishes
    if (endIndex < m_previousBuildOrder.size())
    {
        // there might be a slight difference between planning and execution frames. If nothing is added,
        // we add one action from the previous build order
        BOT_ASSERT(catchUpBuildOrder.size() > 0, "Size of build order must be greater than 0 %i", catchUpBuildOrder.size());
        if (catchUpBuildOrder.size() == 0)
        {
            catchUpBuildOrder.add(m_previousBuildOrder[int(m_previousBuildOrder.size() - m_queue.size())]);
        }
    }

    else
    {
        fixBuildOrder(m_futureGameState, m_results.usefulBuildOrder, 0, false);
        // update GameState with the new build order we found
        for (auto& action : m_results.usefulBuildOrder)
        {
            std::cout << "adding extra action: " << action.first.getName() << std::endl;
            if (action.first.isAbility())
            {
                m_futureGameState.doAbility(action.first, action.second.targetID);
            }
            else
            {
                m_futureGameState.doAction(action.first);
            }
            catchUpBuildOrder.add(action);
            storeBuildOrderInfo(action, m_futureGameState);

            if (m_futureGameState.getCurrentFrame() > gameFrame + searchFrameTime)
            {
                break;
            }
        }
    }

    m_currentBuildOrder = catchUpBuildOrder;
    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::unitsDiedSlowReaction(const std::vector<Unit>& deadUnits)
{
    int gameFrame = m_bot.GetCurrentFrame();
    BOT_ASSERT(m_params.getFrameTimeLimit() - m_futureGameState.getCurrentFrame() == frameLimit, "Wrong calculation");
    int halfwayPoint = int(m_futureGameState.getCurrentFrame() - (frameLimit / 2.f));

    // we do nothing if we are past the halfway point
    if (gameFrame > halfwayPoint)
    {
        std::cout << "units died: past halfway point. Will not react" << std::endl;
        m_deadUnit = true;
        return;
    }

    // if we are before the halfway point, we get rid of all the results we have so far and just start over
    std::cout << "units died: before halfway point. reacting" << std::endl;

    finishSearch();
    setCurrentGameState(true);
    // fast forward to the end of the build order
    BOSS::BuildOrderAbilities buildOrder;
    for (int index = int(m_currentBuildOrder.size() - m_queue.size()); index < m_currentBuildOrder.size(); ++index)
    {
        buildOrder.add(m_currentBuildOrder[index]);
    }
    doBuildOrder(buildOrder);

    m_searchResults.clear();
    m_searchState = SearchState::Free;
}

void BOSSManager::unitsDied(const std::vector<Unit> & deadUnits)
{
    bool needFastReaction = false;
    // need to react fast if a building died
    for (const auto& deadUnit : deadUnits)
    {
        if (!deadUnit.getType().isCombatUnit() && !deadUnit.getType().isWorker())
        {
            needFastReaction = true;
        }
    }

    if (m_fastReaction || needFastReaction)
    {
        finishSearch();

        int startingIndexBeforeCleanUp = int(m_currentBuildOrder.size() - m_queue.size());
        setCurrentGameState(true);

        std::cout << "reacting fast to dead unit" << std::endl;
        unitsDiedFastReaction(deadUnits, startingIndexBeforeCleanUp);

        m_queue.clearAll();
        addToQueue(m_currentBuildOrder);

        m_needFastReaction = true;
    }
    else
    {
        std::cout << "reacting slow to dead unit" << std::endl;
        unitsDiedSlowReaction(deadUnits);
    }    
}

void BOSSManager::doBuildOrder(const BuildOrder & inputBuildOrder)
{
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
    //std::cout << "CC DoBuildOrder called" << std::endl;
    BOSS::Tools::DoBuildOrder(m_futureGameState, BOSSBuildOrder);

    m_currentBuildOrder = BOSSBuildOrder;
}

void BOSSManager::doBuildOrder(BOSS::BuildOrderAbilities & buildOrder)
{
    //std::cout << "BOSS DoBuildOrder called" << std::endl;
    //doBuildingsInQueue(m_futureGameState);
    BOSS::Tools::DoBuildOrder(m_futureGameState, buildOrder);
}

void BOSSManager::doBuildingsInQueue(BOSS::GameState& state) const
{
    auto queuedBuildings = m_buildingManager.getUnassignedQueued();
    for (int index = 0; index < queuedBuildings.size(); ++index)
    {
        const auto& action = BOSS::ActionTypes::GetActionType(queuedBuildings[index].getName());
        std::cout << "doing queued action: " << action.getName() << std::endl;
        BOT_ASSERT(!action.isAbility(), "Abilities should not be in queue");
        state.doAction(action);
        system("pause");
    }
}

void BOSSManager::fixBuildOrder(const BOSS::GameState & state, BOSS::BuildOrderAbilities & buildOrder, int startingIndex, bool doTheBuildingsInQueue)
{
    std::cout << "fixing build order" << std::endl;
    buildOrder.print();

    std::cout << "starting index: " << startingIndex << std::endl;
    
    // adds in buildings currently in production queue
    BOSS::GameState tempState(state);
    if (doTheBuildingsInQueue)
    {
        doBuildingsInQueue(tempState);
    }

    if (fixBuildOrderRecurse(tempState, buildOrder, startingIndex))
    {
        std::cout << "finished fixing build order" << std::endl;
    }
    else
    {
        BOSS::BuildOrderAbilities newBuildOrder;
        std::cout << "fixing build order failed. taking actions until the first chronoboost" << std::endl;
        int index = startingIndex;
        while (index < buildOrder.size())
        {
            if (buildOrder[index].first.isAbility())
            {
                break;
            }
            else
            {
                newBuildOrder.add(buildOrder[index]);
            }
            ++index;
        }
        m_queue.clearAll();
        addToQueue(newBuildOrder);
        buildOrder = newBuildOrder;
    }    
}

bool BOSSManager::fixBuildOrderRecurse(const BOSS::GameState & state, BOSS::BuildOrderAbilities & buildOrder, int index) const
{
    if (index >= buildOrder.size())
    {
        return true;
    }

    BOSS::GameState currentState(state);
    auto actionType = buildOrder[index].first;
    std::cout << "action: " << actionType.getName() << std::endl;

    if (actionType.isAbility())
    {
        // target of this action no longer exists, so we remove it from the build order
        if (!currentState.haveType(buildOrder[index].second.targetType))
        {
            buildOrder.remove(index);
            std::cout << "removed action: " << actionType.getName() << " cause we don't have builder or prereqs" << std::endl;
            return fixBuildOrderRecurse(currentState, buildOrder, index);
        }
        
        else
        {
            auto targets = currentState.getAbilityTargetUnit(buildOrder[index]);

            // no valid target. we have chosen a wrong target somewhere before
            if (targets.size() == 0)
            {
                return false;
            }

            // try out all possible combinations of targets
            for (const auto& target : targets)
            {
                currentState.doAbility(actionType, target.first);
                buildOrder[index].second.targetID = target.first;
                buildOrder[index].second.targetProductionID = target.second;
                buildOrder[index].second.frameCast = currentState.getCurrentFrame();

                // we found a good target
                if (fixBuildOrderRecurse(currentState, buildOrder, index + 1))
                {
                    return true;
                }
            }

            // no valid targets found. we messed up a target somewhere before
            return false;
        }
    }
    else
    {
        // if a builder or prereq no longer exists then we remove the action
        if (currentState.haveBuilder(actionType) && currentState.havePrerequisites(actionType))
        {
            currentState.doAction(actionType);
            return fixBuildOrderRecurse(currentState, buildOrder, index + 1);
        }
        else
        {
            buildOrder.remove(index);
            std::cout << "removed action: " << actionType.getName() << " cause we don't have builder or prereqs" << std::endl;
            return fixBuildOrderRecurse(currentState, buildOrder, index);
        }
        
    }
}

void BOSSManager::printDebugInfo() const
{
    if (!m_bot.Config().DrawBOSSInfo)
    {
        return;
    }

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

    if (m_futureGameState.getCurrentFrame() > 1)
    {
        ss << m_unitInfo;

        ss << "\nNodes visited: " << m_results.nodeVisits << "\n";
        ss << "Nodes expanded: " << m_results.nodesExpanded << "\n";
        ss << "Search time: " << m_results.timeElapsed / 1000 << "\n";
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
        size_t avgBuildOrderSize = 0;
        double avgBuildOrderValue = 0;
        float highestBuildOrderValue = 0;
        for (auto& result : m_searchResults)
        {
            avgTime += result.timeElapsed;
            nodesExpanded += result.nodesExpanded;
            nodesVisited += result.nodeVisits;
            avgBuildOrderSize += result.usefulBuildOrder.size();
            avgBuildOrderValue += result.usefulEval;
            highestBuildOrderValue = std::max(highestBuildOrderValue, result.usefulEval);
        }
        avgTime /= (m_searchResults.size() * 1000);
        nodesExpanded /= m_searchResults.size();
        nodesVisited /= m_searchResults.size();
        avgBuildOrderSize /= m_searchResults.size();
        avgBuildOrderValue /= m_searchResults.size();

        ss << "\nNext build order search stats\n\n";
        ss << "Searches completed: " << m_searchResults.size() << "\n";
        ss << "Average nodes visited: " << nodesVisited << "\n";
        ss << "Average nodes expanded: " << nodesExpanded << "\n";
        ss << "Average search length: " << avgTime << "\n";
        ss << "Average build order length: " << avgBuildOrderSize << "\n";
        ss << "Average build order value: " << avgBuildOrderValue << "\n";
        ss << "Highest build order value: " << highestBuildOrderValue << "\n";
    }

    m_bot.Map().drawTextScreen(0.72f, 0.05f, ss.str(), CCColor(255, 255, 0));

    std::stringstream weights_ss;
    weights_ss << "Unit Weights\n\n";
    auto unitWeights = BOSS::Eval::GetUnitWeightVector();
    for (int index = 0; index < unitWeights.size(); ++index)
    {
        BOSS::FracType weight = unitWeights[index];
        weights_ss << BOSS::ActionTypes::GetActionType(index + 1).getName() << ": " << weight << "\n";
    }

    m_bot.Map().drawTextScreen(0.10f, 0.02f, weights_ss.str(), CCColor(255, 255, 0));
}