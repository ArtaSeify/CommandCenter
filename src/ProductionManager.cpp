#include "ProductionManager.h"
#include "Util.h"
#include "CCBot.h"

using namespace CC;

ProductionManager::ProductionManager(CCBot & bot)
    : m_bot             (bot)
    , m_buildingManager (bot)
    , m_queue           (bot)
    , m_BOSSManager     (bot)
{

}

void ProductionManager::setBuildOrder(const BuildOrder & buildOrder)
{
    m_queue.clearAll();

    for (size_t i(0); i<buildOrder.size(); ++i)
    {
        m_queue.queueAsLowestPriority(buildOrder[i], true);
    }
}

void ProductionManager::onStart()
{
    m_buildingManager.onStart();
    setBuildOrder(m_bot.Strategy().getOpeningBookBuildOrder());
}

void ProductionManager::onFrame()
{
    searchBuildOrder();
    fixBuildOrderDeadlock();
    manageBuildOrderQueue();

    // TODO: if nothing is currently building, get a new goal from the strategy manager
    // TODO: detect if there's a build order deadlock once per second
    // TODO: triggers for game things like cloaked units etc

    m_buildingManager.onFrame();
    drawProductionInformation();
}

// on unit destroy
void ProductionManager::onUnitDestroy(const Unit & unit)
{
    // TODO: might have to re-do build order if a vital unit died
}

void ProductionManager::searchBuildOrder()
{
    // currently searching for a build order
    if (m_BOSSManager.searchInProgress())
    {
        // if the current queue is empty, stop the search so we have stuff to build
        if (m_queue.isEmpty())
        {
            m_BOSSManager.finishSearch();
        }
        return;
    }

    if (m_BOSSManager.isSearchFinished())
    {
        searchFinished();
    }

    m_BOSSManager.setCurrentGameState();

    const int framesToSearch = 4032;

    // actions to search over
    std::vector<std::string> relevantActionsNames =
    { "ChronoBoost", "Probe", "Pylon", "Nexus", "Assimilator", "Gateway", "CyberneticsCore", "Stalker",
        "Zealot", "Colossus", "Forge", "FleetBeacon", "TwilightCouncil", "Stargate", "TemplarArchive",
        "DarkShrine", "RoboticsBay", "RoboticsFacility", "ZealotWarped", "Zealot", "StalkerWarped", "Stalker",
        "HighTemplarWarped", "HighTemplar", "DarkTemplarWarped", "DarkTemplar", "SentryWarped", "Sentry",
        "Phoenix", "Carrier", "VoidRay", "WarpPrism", "Observer", "Immortal", "Probe", "Interceptor", "AdeptWarped", 
        "Adept", "Oracle", "Tempest", "Disruptor", "WarpGateResearch" };
    
        /*"Adept", "Sentry", "Colossus", "FleetBeacon", "Forge",
        "TwilightCouncil", "Stargate", "TemplarArchive", "DarkShrine", 
        "RoboticsBay", "RoboticsFacility", "HighTemplar", "DarkTemplar", "Phoenix", "Carrier", 
        "VoidRay", "WarpPrism", "Observer", "Immortal", "Oracle", "Tempest", "Disruptor"};*/
    BOSS::ActionSetAbilities relevantActions;
    for (std::string & actionName : relevantActionsNames)
    {
        relevantActions.add(BOSS::ActionTypes::GetActionType(actionName));
    }

    // how many times we are allowed to do an action. if no limit is given, then we can do it
    // as many times as we want
    std::vector<std::pair<BOSS::ActionType, int>> maxActions;
    //maxActions.emplace_back(BOSS::ActionTypes::GetActionType("Nexus"), 2);
    //maxActions.emplace_back(BOSS::ActionTypes::GetActionType("Nexus"), 2);
    /*maxActions.emplace_back(BOSS::ActionTypes::GetActionType("Gateway"), 4);
    maxActions.emplace_back(BOSS::ActionTypes::GetActionType("CyberneticsCore"), 1);
    maxActions.emplace_back(BOSS::ActionTypes::GetActionType("Pylon"), m_BOSSManager.numSupplyProviders() + (framesToSearch / 1000) + 4);*/

    // the opening build order to follow inside the search
    BOSS::BuildOrderAbilities openingBuildOrder;

    bool sortActions = false;

    m_BOSSManager.setParameters(framesToSearch, 2000000, true, sortActions, maxActions, openingBuildOrder, relevantActions);

    m_BOSSManager.startSearch();
}

void ProductionManager::searchFinished()
{
    auto & BOSSBuildOrder = m_BOSSManager.getBuildOrder();
    m_BOSSManager.gotData();

    addToBuildOrder(BOSSBuildOrder);   
}

void ProductionManager::addToBuildOrder(const BOSS::BuildOrderAbilities & BOSSBuildOrder)
{
    for (auto & actionTargetPair : BOSSBuildOrder)
    {
        auto & actionType = actionTargetPair.first;
        auto & target = actionTargetPair.second;

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

void ProductionManager::manageBuildOrderQueue()
{
    // if there is nothing in the queue, oh well
    if (m_queue.isEmpty())
    {
        return;
    }

    // the current item to be used
    BuildOrderItem & currentItem = m_queue.getHighestPriorityItem();

    // while there is still something left in the queue
    while (!m_queue.isEmpty())
    {
        // this is the unit which can produce the currentItem
        Unit producer = getProducer(currentItem.type);

        // check to see if we can make it right now
        bool canMake = canMakeNow(producer, currentItem.type);

        // TODO: if it's a building and we can't make it yet, predict the worker movement to the location

        // if we can make the current item
        if (producer.isValid() && canMake)
        {
            // create it and remove it from the _queue
            create(producer, currentItem);
            m_queue.removeCurrentHighestPriorityItem();

            // don't actually loop around in here
            break;
        }
        // otherwise, if we can skip the current item
        else if (m_queue.canSkipItem())
        {
            // skip it
            m_queue.skipItem();

            // and get the next one
            currentItem = m_queue.getNextHighestPriorityItem();
        }
        else
        {
            // so break out
            break;
        }
    }
}

void ProductionManager::fixBuildOrderDeadlock()
{
    if (m_queue.isEmpty()) { return; }
    BuildOrderItem & currentItem = m_queue.getHighestPriorityItem();

    // check to see if we have the prerequisites for the topmost item
    bool hasRequired = m_bot.Data(currentItem.type).requiredUnits.empty();
    for (auto & required : m_bot.Data(currentItem.type).requiredUnits)
    {
        if (m_bot.UnitInfo().getUnitTypeCount(Players::Self, required, false) > 0 || m_buildingManager.isBeingBuilt(required))
        {
            hasRequired = true;
            break;
        }
    }

    if (!hasRequired)
    {
        std::cout << currentItem.type.getName() << " needs " << m_bot.Data(currentItem.type).requiredUnits[0].getName() << "\n";
        m_queue.queueAsHighestPriority(MetaType(m_bot.Data(currentItem.type).requiredUnits[0], m_bot), true);
        fixBuildOrderDeadlock();
        return;
    }

    // build the producer of the unit if we don't have one
    bool hasProducer = m_bot.Data(currentItem.type).whatBuilds.empty();
    for (auto & producer : m_bot.Data(currentItem.type).whatBuilds)
    {
        if (m_bot.UnitInfo().getUnitTypeCount(Players::Self, producer, false) > 0 || m_buildingManager.isBeingBuilt(producer))
        {
            hasProducer = true;
            break;
        }
    }

    if (!hasProducer)
    {
        m_queue.queueAsHighestPriority(MetaType(m_bot.Data(currentItem.type).whatBuilds[0], m_bot), true);
        fixBuildOrderDeadlock();
    }

    // build a refinery if we don't have one and the thing costs gas
    auto refinery = Util::GetRefinery(m_bot.GetPlayerRace(Players::Self), m_bot);
    if (m_bot.Data(currentItem.type).gasCost > 0 && m_bot.UnitInfo().getUnitTypeCount(Players::Self, refinery, false) == 0)
    {
        m_queue.queueAsHighestPriority(MetaType(refinery, m_bot), true);
    } 

    // build supply if we need some
    auto supplyProvider = Util::GetSupplyProvider(m_bot.GetPlayerRace(Players::Self), m_bot);
    if (m_bot.Data(currentItem.type).supplyCost > (m_bot.GetMaxSupply() - m_bot.GetCurrentSupply()) && !m_buildingManager.isBeingBuilt(supplyProvider))
    {
        m_queue.queueAsHighestPriority(MetaType(supplyProvider, m_bot), true);
    }
}

Unit ProductionManager::getProducer(const MetaType & type, CCPosition closestTo)
{
    // get all the types of units that can build this type
    auto & producerTypes = m_bot.Data(type).whatBuilds;

    // make a set of all candidate producers
    std::vector<Unit> candidateProducers;
    for (auto unit : m_bot.UnitInfo().getUnits(Players::Self))
    {
        // reasons a unit can not train the desired type
        if (std::find(producerTypes.begin(), producerTypes.end(), unit.getType()) == producerTypes.end()) { continue; }
        if (!unit.isCompleted()) { continue; }
        if (!type.isAbility() && m_bot.Data(unit).isBuilding && unit.isTraining()) { continue; }
        if (unit.isFlying()) { continue; }
        // WarpGates need special consideration because the building doesn't actually produce the unit, it casts an ability that does
        if (m_bot.GetPlayerRace(Players::Self) == CCRace::Protoss && unit.getType().isMorphedBuilding()) 
        { 
            auto & abilities = m_bot.Query()->GetAbilitiesForUnit(unit.getUnitPtr()).abilities;
            if (std::find_if(abilities.begin(), abilities.end(), 
                [type](const sc2::AvailableAbility & ability) { return ability.ability_id == type.getAbility().first; }) == abilities.end())
            {
                continue;
            }
        }

        // TODO: if unit is not powered continue
        //if (m_bot.GetPlayerRace(Players::Self) == CCRace::Protoss && unit.getType().isBuilding() && !unit.isPowered()) { continue; }
        // TODO: if the type is an addon, some special cases
        // TODO: if the type requires an addon and the producer doesn't have one

        // if we haven't cut it, add it to the set of candidates
        candidateProducers.push_back(unit);
    }

    return getClosestUnitToPosition(candidateProducers, closestTo);
}

Unit ProductionManager::getClosestUnitToPosition(const std::vector<Unit> & units, CCPosition closestTo)
{
    if (units.size() == 0)
    {
        return Unit();
    }

    // if we don't care where the unit is return the first one we have
    if (closestTo.x == 0 && closestTo.y == 0)
    {
        return units[0];
    }

    Unit closestUnit;
    double minDist = std::numeric_limits<double>::max();

    for (auto & unit : units)
    {
        double distance = Util::Dist(unit, closestTo);
        if (!closestUnit.isValid() || distance < minDist)
        {
            closestUnit = unit;
            minDist = distance;
        }
    }

    return closestUnit;
}

// this function will check to see if all preconditions are met and then create a unit
void ProductionManager::create(const Unit & producer, BuildOrderItem & item)
{
    if (!producer.isValid())
    {
        return;
    }

    // if we're dealing with a building
    if (item.type.isBuilding())
    {
        if (item.type.getUnitType().isMorphedBuilding())
        {
            producer.morph(item.type.getUnitType());
            std::cout << producer.getPosition().x << "," << producer.getPosition().y << std::endl;
            //std::cout << "morphing!" << std::endl;
        }
        else
        {
            m_buildingManager.addBuildingTask(item.type.getUnitType(), Util::GetTilePosition(m_bot.GetStartLocation()));
            //std::cout << "building!" << std::endl;
        }
        
    }
    // warp in unit
    else if (item.type.isUnit() && item.type.getName().find("Warped") != std::string::npos)
    {
        //std::cout << "warping unit!" << std::endl;
        // get the closest warp in pylon or warpprism to the enemy base
        std::vector<Unit> pylonsAndWarpPrisms;
        float closest_to_enemy_pylon_pos = std::numeric_limits<float>::max();
        CCPosition powerPos;
        for (auto & unit : m_bot.UnitInfo().getUnits(Players::Self))
        {
            if (unit.getType().getAPIUnitType() == sc2::UNIT_TYPEID::PROTOSS_PYLON || unit.getType().getAPIUnitType() == sc2::UNIT_TYPEID::PROTOSS_WARPPRISMPHASING)
            {
                pylonsAndWarpPrisms.push_back(unit);
                float distance = m_bot.Query()->PathingDistance(unit.getPosition(), m_bot.Bases().getPlayerStartingBaseLocation(Players::Enemy)->getPosition());
                if (distance < closest_to_enemy_pylon_pos)
                {
                    closest_to_enemy_pylon_pos = distance;
                    powerPos = unit.getPosition();
                }
            }
        }

        CCPosition warpPosition(-1, -1);
        float closest_distance = std::numeric_limits<float>::max();

        // get the closest point in the power sphere to the enemy base
        for (int x = -5; x < 6; ++x)
        {
            for (int y = -5; y < 6; ++y)
            {
                CCPosition pos = CCPosition(powerPos.x + x, powerPos.y + y);
                if (m_bot.Map().isVisible(int(pos.x), int(pos.y)) && m_bot.Query()->Placement(m_bot.Data(item.type.getUnitType()).warpAbility, pos)
                    && Util::Dist(getClosestUnitToPosition(m_bot.UnitInfo().getUnits(Players::Self), pos).getPosition(), pos) > item.type.getUnitType().tileWidth() / 2)
                {
                    float distToEnemyBase = m_bot.Query()->PathingDistance(pos, m_bot.Bases().getPlayerStartingBaseLocation(Players::Enemy)->getPosition());
                    if (distToEnemyBase < closest_distance)
                    {
                        warpPosition = pos;
                        closest_distance = distToEnemyBase;
                    }
                    //std::cout << "warp-in is possible in location: " << powerPos.x + x << "," << powerPos.y + y << std::endl;
                }
                else
                {
                    //std::cout << "unable to warp in location: " << powerPos.x + x << "," << powerPos.y + y << std::endl;
                }
            }
        }

        // couldn't find a location to warp with this pylon
        if (!m_bot.Map().isValidPosition(warpPosition))
        {
            std::cout << "closest pylon wasn't valid!" << std::endl;
            std::random_device dev;
            std::mt19937 rng(dev());
            std::uniform_int_distribution<std::mt19937::result_type> dist(-5, 6); // distribution in range [-5, 5]

            // go through every pylon and warp prism
            for (auto & powerStructure : pylonsAndWarpPrisms)
            {
                CCPosition powerPos = powerStructure.getPosition();
                // try 30 random positions within the power grid
                for (int count = 0; count < 30; ++count)
                {
                    int dx = dist(rng);
                    int dy = dist(rng);

                    CCPosition pos = CCPosition(powerPos.x + dx, powerPos.y + dy);
                    // take the first valid position you find
                    if (m_bot.Map().isVisible(int(pos.x), int(pos.y)) && m_bot.Query()->Placement(m_bot.Data(item.type.getUnitType()).warpAbility, pos) 
                        && Util::Dist(getClosestUnitToPosition(m_bot.UnitInfo().getUnits(Players::Self), pos).getPosition(), pos) > item.type.getUnitType().tileWidth() / 2)
                    {
                        warpPosition = pos;
                        break;
                    }
                }
            }
        }

        producer.warp(item.type.getUnitType(), warpPosition);
        std::cout << "warping! " << warpPosition.x << "," << warpPosition.y << std::endl;
    }
    // if we're dealing with a non-building unit
    else if (item.type.isUnit())
    {
        producer.train(item.type.getUnitType());
        //std::cout << "training unit!" << std::endl;
    }
    else if (item.type.isUpgrade())
    {
        producer.research(item.type.getAbility().first);
        //std::cout << "researching upgrade!" << std::endl;
    }
    else if (item.type.isAbility())
    {
        const AbilityType & action = item.type.getAbility();

        for (auto & unit : m_bot.UnitInfo().getUnits(Players::Self))
        {            
            if (unit.getType() == action.second.target_type && unit.getUnitPtr()->orders.size() > 0)
            {                
                if (unit.getUnitPtr()->orders[0].ability_id == action.second.targetProduction_ability)
                {
                    producer.cast(m_bot.GetUnit(unit.getID()), action.first);
                    //std::cout << "casting ability!" << std::endl;
                    return;
                }
            }
        }        
        std::cerr << "Could not find target for chronoboost!" << std::endl;
    }
}

bool ProductionManager::canMakeNow(const Unit & producer, const MetaType & type)
{
    if (!producer.isValid() || !meetsReservedResources(type))
    {
        return false;
    }

    // can't use chronoboost if the nexus has less than 50 energy
    if (type.isAbility() && type.getAbility().first == sc2::ABILITY_ID::EFFECT_CHRONOBOOST)
    {
        if (producer.getEnergy() < 50)
        {
            return false;
        }
        
        return true;        
    }

#ifdef SC2API
    sc2::AvailableAbilities available_abilities = m_bot.Query()->GetAbilitiesForUnit(producer.getUnitPtr(), true);

    // quick check if the unit can't do anything it certainly can't build the thing we want
    if (available_abilities.abilities.empty())
    {
        return false;
    }
    else
    {
        // check to see if one of the unit's available abilities matches the build ability type
        sc2::AbilityID MetaTypeAbility;
        if (type.getName().find("Warped") != std::string::npos)
        {
            MetaTypeAbility = m_bot.Data(type).warpAbility;
        }
        else
        {
            MetaTypeAbility = m_bot.Data(type).buildAbility; 
        }

        for (const sc2::AvailableAbility & available_ability : available_abilities.abilities)
        {
            if (available_ability.ability_id == MetaTypeAbility)
            {
                return true;
            }
        }
    }

    return false;
#else
    bool canMake = meetsReservedResources(type);
    if (canMake)
    {
        if (type.isUnit())
        {
            canMake = BWAPI::Broodwar->canMake(type.getUnitType().getAPIUnitType(), producer.getUnitPtr());
        }
        else if (type.isTech())
        {
            canMake = BWAPI::Broodwar->canResearch(type.getTechType(), producer.getUnitPtr());
        }
        else if (type.isUpgrade())
        {
            canMake = BWAPI::Broodwar->canUpgrade(type.getUpgrade(), producer.getUnitPtr());
        }
        else
        {	
            BOT_ASSERT(false, "Unknown type");
        }
    }

    return canMake;
#endif
}

bool ProductionManager::detectBuildOrderDeadlock()
{
    // TODO: detect build order deadlocks here
    return false;
}

int ProductionManager::getFreeMinerals()
{
    return m_bot.GetMinerals() - m_buildingManager.getReservedMinerals();
}

int ProductionManager::getFreeGas()
{
    return m_bot.GetGas() - m_buildingManager.getReservedGas();
}

// return whether or not we meet resources, including building reserves
bool ProductionManager::meetsReservedResources(const MetaType & type)
{
    // return whether or not we meet the resources
    int minerals = m_bot.Data(type).mineralCost;
    int gas = m_bot.Data(type).gasCost;

    return (m_bot.Data(type).mineralCost <= getFreeMinerals()) && (m_bot.Data(type).gasCost <= getFreeGas());
}

void ProductionManager::drawProductionInformation()
{
    if (!m_bot.Config().DrawProductionInfo)
    {
        return;
    }

    std::stringstream ss;
    ss << "Production Information\n\n";

    for (auto & unit : m_bot.UnitInfo().getUnits(Players::Self))
    {
        if (unit.isBeingConstructed())
        {
            //ss << sc2::UnitTypeToName(unit.unit_type) << " " << unit.build_progress << "\n";
        }
    }

    ss << m_queue.getQueueInformation();

    m_bot.Map().drawTextScreen(0.01f, 0.01f, ss.str(), CCColor(255, 255, 0));
}
