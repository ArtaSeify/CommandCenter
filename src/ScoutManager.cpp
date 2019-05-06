#include "ScoutManager.h"
#include "CCBot.h"
#include "Util.h"

using namespace CC;

ScoutManager::ScoutManager(CCBot & bot)
    : m_bot             (bot)
    , m_scoutUnit       ()
    , m_numScouts       (0)
    , m_scoutUnderAttack(false)
    , m_scoutStatus     ("None")
    , m_previousScoutHP (0)
{
}

void ScoutManager::onStart()
{

}

void ScoutManager::onFrame()
{
    moveScouts();
    drawScoutInformation();
}

void ScoutManager::setWorkerScout(const Unit & unit)
{
    // if we have a previous worker scout, release it back to the worker manager
    if (m_scoutUnit.isValid() && m_scoutUnit.getType().isWorker())
    {
        m_bot.Workers().finishedWithWorker(m_scoutUnit);
    }

    m_scoutUnit = unit;
    m_bot.Workers().setScoutWorker(m_scoutUnit);
}

void ScoutManager::setCombatUnitScout(const Unit& unit)
{
    if (m_scoutUnit.isValid() && m_scoutUnit.getType().isWorker())
    {
        m_bot.Workers().finishedWithWorker(m_scoutUnit);
    }

    m_scoutUnit = unit;
}

void ScoutManager::drawScoutInformation()
{
    if (!m_bot.Config().DrawScoutInfo)
    {
        return;
    }

    std::stringstream ss;
    ss << "Scout Info: " << m_scoutStatus;

    m_bot.Map().drawTextScreen(0.1f, 0.6f, ss.str());
}

void ScoutManager::moveScouts()
{
    auto scoutUnit = m_scoutUnit;

    if (!scoutUnit.isValid()) { return; }

    // scout is dead
    if (std::find(m_bot.UnitInfo().getUnitsDied(Players::Self).begin(), m_bot.UnitInfo().getUnitsDied(Players::Self).end(), scoutUnit) != m_bot.UnitInfo().getUnitsDied(Players::Self).end())
    {
        m_scoutStatus = "Dead";
        m_scoutUnit = Unit();
        return;
    }

    CCHealth scoutHP = scoutUnit.getHitPoints() + scoutUnit.getShields();

    // get the enemy base location, if we have one
    const BaseLocation * enemyBaseLocation = m_bot.Bases().getPlayerStartingBaseLocation(Players::Enemy);

    int scoutDistanceThreshold = 12;

    // if we know where the enemy region is and where our scout is
    if (enemyBaseLocation)
    {
        int scoutDistanceToEnemy = m_bot.Map().getGroundDistance(scoutUnit.getPosition(), enemyBaseLocation->getPosition());
        bool scoutInRangeOfenemy = enemyBaseLocation->containsPosition(scoutUnit.getPosition());

        // we only care if the scout is under attack within the enemy region
        // this ignores if their scout worker attacks it on the way to their base
        if (scoutHP < m_previousScoutHP || enemyCombatUnitInRadiusOf(scoutUnit.getPosition()))
        {
            m_scoutUnderAttack = true;
        }

        else if (scoutHP == m_previousScoutHP && !enemyWorkerInRadiusOf(scoutUnit.getPosition()))
        {
            m_scoutUnderAttack = false;
        }

        // if the scout is in the enemy region
        if (scoutInRangeOfenemy)
        {
            // get the closest enemy worker
            Unit closestEnemyWorkerUnit = closestEnemyWorkerTo(scoutUnit.getPosition());

            // if the worker scout is not under attack
            if (!m_scoutUnderAttack)
            {
                // if there is a worker nearby, harass it
                if (m_bot.Config().ScoutHarassEnemy && closestEnemyWorkerUnit.isValid() && (Util::Dist(scoutUnit, closestEnemyWorkerUnit) < scoutDistanceThreshold))
                {
                    m_scoutStatus = "Harass enemy worker";
                    m_scoutUnit.attackUnit(closestEnemyWorkerUnit);
                }
                // otherwise keep moving to the enemy base location
                else
                {
                    m_scoutStatus = "Moving to enemy base location";
                    m_scoutUnit.move(enemyBaseLocation->getPosition());
                }
            }
            // if the worker scout is under attack
            else
            {
                m_scoutStatus = "Under attack inside, fleeing";
                m_scoutUnit.move(getFleePosition());
            }
        }
        // if the scout is not in the enemy region
        else if (m_scoutUnderAttack)
        {
            m_scoutStatus = "Under attack outside, fleeing";

            m_scoutUnit.move(getFleePosition());
        }
        else
        {
            m_scoutStatus = "Enemy region known, going there";

            // move to the enemy region
            m_scoutUnit.move(enemyBaseLocation->getPosition());
        }

    }

    // for each start location in the level
    if (!enemyBaseLocation)
    {
        m_scoutStatus = "Enemy base unknown, exploring";

        for (const BaseLocation * startLocation : m_bot.Bases().getStartingBaseLocations())
        {
            // if we haven't explored it yet then scout it out
            // TODO: this is where we could change the order of the base scouting, since right now it's iterator order
            if (!m_bot.Map().isExplored(startLocation->getPosition()))
            {
                m_scoutUnit.move(startLocation->getPosition());
                return;
            }
        }
    }

    m_previousScoutHP = scoutHP;
}

Unit ScoutManager::closestEnemyWorkerTo(const CCPosition & pos) const
{
    if (!m_scoutUnit.isValid()) { return Unit(); }

    Unit enemyWorker;
    float minDist = std::numeric_limits<float>::max();

    // for each enemy worker
    for (auto & unit : m_bot.UnitInfo().getUnits(Players::Enemy))
    {
        if (unit.getType().isWorker())
        {
            float dist = Util::Dist(unit, m_scoutUnit);

            if (dist < minDist)
            {
                minDist = dist;
                enemyWorker = unit;
            }
        }
    }

    return enemyWorker;
}
bool ScoutManager::enemyWorkerInRadiusOf(const CCPosition & pos) const
{
    for (auto & unit : m_bot.UnitInfo().getUnits(Players::Enemy))
    {
        if (unit.getType().isWorker() && Util::Dist(unit, pos) < 10)
        {
            return true;
        }
    }

    return false;
}

bool ScoutManager::enemyCombatUnitInRadiusOf(const CCPosition& pos) const
{
    for (auto& unit : m_bot.UnitInfo().getUnits(Players::Enemy))
    {
        if (unit.getType().isCombatUnit() && Util::Dist(unit, pos) < 10)
        {
            return true;
        }
    }

    return false;
}

CCPosition ScoutManager::getFleePosition() const
{
    // TODO: make this follow the perimeter of the enemy base again, but for now just use home base as flee direction
    return m_bot.GetStartLocation();
}