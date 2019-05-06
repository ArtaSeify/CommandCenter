#include "GameCommander.h"
#include "CCBot.h"
#include "Util.h"

using namespace CC;

GameCommander::GameCommander(CCBot & bot)
    : m_bot                 (bot)
    , m_productionManager   (bot)
    , m_scoutManager        (bot)
    , m_combatCommander     (bot)
    , m_initialScoutSet     (false)
{
    
}

void GameCommander::onStart()
{
    m_productionManager.onStart();
    m_scoutManager.onStart();
    m_combatCommander.onStart();
}

void GameCommander::onFrame()
{
    m_timer.start();

    handleUnitAssignments();

    m_productionManager.onFrame();
    m_scoutManager.onFrame();
    m_combatCommander.onFrame(m_combatUnits);

    drawDebugInterface();
}

void GameCommander::drawDebugInterface()
{
    drawGameInformation(4, 1);
}

void GameCommander::drawGameInformation(int x, int y)
{
    std::stringstream ss;
    ss << "Players: " << "\n";
    ss << "Strategy: " << m_bot.Config().StrategyName << "\n";
    ss << "Map Name: " << "\n";
    ss << "Time: " << "\n";
}

// assigns units to various managers
void GameCommander::handleUnitAssignments()
{
    m_validUnits.clear();
    m_combatUnits.clear();

    // filter our units for those which are valid and usable
    setValidUnits();

    // set each type of unit
    setCombatUnits();
    setScoutUnits();
}

bool GameCommander::isAssigned(const Unit & unit) const
{
    return     (std::find(m_combatUnits.begin(), m_combatUnits.end(), unit) != m_combatUnits.end())
        || (std::find(m_scoutUnits.begin(), m_scoutUnits.end(), unit) != m_scoutUnits.end());
}

// validates units as usable for distribution to various managers
void GameCommander::setValidUnits()
{
    // make sure the unit is completed and alive and usable
    for (auto & unit : m_bot.UnitInfo().getUnits(Players::Self))
    {
        m_validUnits.push_back(unit);
    }
}

void GameCommander::setScoutUnits()
{
    // remove dead units
    for (auto it = m_scoutUnits.begin(); it != m_scoutUnits.end(); )
    {
        if (!it->isAlive())
        {
            it = m_scoutUnits.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // if we haven't set a scout unit, do it
    if (m_scoutUnits.empty() && !m_initialScoutSet)
    {
        // if it exists
        if (shouldSendInitialScout())
        {
            // grab the closest worker to the supply provider to send to scout
            Unit workerScout = m_bot.Workers().getClosestWorkerTo(m_bot.GetStartLocation());

            // if we find a worker (which we should) add it to the scout units
            if (workerScout.isValid())
            {
                m_scoutManager.setWorkerScout(workerScout);
                assignUnit(workerScout, m_scoutUnits);
                m_initialScoutSet = true;
            }
        }
    }

    // keep resending the fastest combat unit as a scout so we have as much information as possible
    else if (m_scoutUnits.empty() && m_initialScoutSet)
    {
        Unit combatUnitScout = getFastestCombatUnit();

        // if we find a combat unit
        if (combatUnitScout.isValid())
        {
            m_scoutManager.setCombatUnitScout(combatUnitScout);
            assignUnit(combatUnitScout, m_scoutUnits);
        }
    }
}

Unit GameCommander::getFastestCombatUnit() const
{
    float fastestSpeed = 0;
    Unit fastestUnit;

    for (auto& unit : m_combatUnits)
    {
        BOT_ASSERT(unit.isValid(), "Have a null unit in our combat units");

        float unitSpeed = m_bot.Observation()->GetUnitTypeData()[unit.getUnitPtr()->unit_type].movement_speed;

        if (unitSpeed > fastestSpeed)
        {
            fastestUnit = unit;
            fastestSpeed = unitSpeed;
        }
    }

    return fastestUnit;
}

bool GameCommander::shouldSendInitialScout() const
{
    return m_bot.Strategy().scoutConditionIsMet();
}

// sets combat units to be passed to CombatCommander
void GameCommander::setCombatUnits()
{
    for (auto & unit : m_validUnits)
    {
        BOT_ASSERT(unit.isValid(), "Have a null unit in our valid units\n");

        if (!isAssigned(unit) && unit.getType().isCombatUnit())
        {
            assignUnit(unit, m_combatUnits);
        }
    }
}

void GameCommander::onUnitCreate(const Unit & unit)
{

}

void GameCommander::onUnitDestroy(const Unit & unit)
{
    //_productionManager.onUnitDestroy(unit);
}


void GameCommander::assignUnit(const Unit & unit, std::vector<Unit> & units)
{
    if (std::find(m_scoutUnits.begin(), m_scoutUnits.end(), unit) != m_scoutUnits.end())
    {
        m_scoutUnits.erase(std::remove(m_scoutUnits.begin(), m_scoutUnits.end(), unit), m_scoutUnits.end());
    }
    else if (std::find(m_combatUnits.begin(), m_combatUnits.end(), unit) != m_combatUnits.end())
    {
        m_combatUnits.erase(std::remove(m_combatUnits.begin(), m_combatUnits.end(), unit), m_combatUnits.end());
    }

    units.push_back(unit);
}
