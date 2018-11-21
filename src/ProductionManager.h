#pragma once

#include "Common.h"
#include "BuildOrder.h"
#include "BuildingManager.h"
#include "BuildOrderQueue.h"

using namespace CCUnit;

class CCBot;

class ProductionManager
{
    CCBot &       m_bot;

    BuildingManager m_buildingManager;
    BuildOrderQueue m_queue;

    CCUnit::Unit    getClosestUnitToPosition(const std::vector<CCUnit::Unit> & units, CCPosition closestTo);
    bool            meetsReservedResources(const MetaType & type);
    bool            canMakeNow(const CCUnit::Unit & producer, const MetaType & type);
    bool            detectBuildOrderDeadlock();
    void            setBuildOrder(const BuildOrder & buildOrder);
    void            create(const CCUnit::Unit & producer, BuildOrderItem & item);
    void            manageBuildOrderQueue();
    int             getFreeMinerals();
    int             getFreeGas();

    void    fixBuildOrderDeadlock();

public:

    ProductionManager(CCBot & bot);

    void    onStart();
    void    onFrame();
    void    onUnitDestroy(const CCUnit::Unit & unit);
    void    drawProductionInformation();

    CCUnit::Unit getProducer(const MetaType & type, CCPosition closestTo = CCPosition(0, 0));
};
