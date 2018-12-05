#pragma once

#include "Common.h"
#include "UnitType.h"

namespace CC
{
    namespace MetaTypes
    {
        enum { Unit, Upgrade, Buff, Tech, Ability, None };
    }

    using AbilityType = std::pair<CCAbility, int>;

    class CCBot;
    class MetaType
    {
        CCBot *         m_bot;
        size_t          m_type;
        std::string     m_name;
        CCRace          m_race;
        UnitType        m_unitType;
        CCUpgrade       m_upgrade;
        AbilityType     m_ability;

#ifndef SC2API
        BWAPI::TechType m_tech;
#endif

    public:

        MetaType();
        MetaType(const std::string & name, CCBot & bot);
        MetaType(const UnitType & unitType, CCBot & bot);
        MetaType(const CCUpgrade & upgradeType, CCBot & bot);
        MetaType(const CCAbility & abilityType, int target, CCBot & bot);

        bool    isUnit()        const;
        bool    isUpgrade()     const;
        bool    isTech()        const;
        bool    isBuilding()    const;

        const size_t &          getMetaType()  const;
        const std::string &     getName()       const;
        const CCRace &          getRace()       const;
        const UnitType &        getUnitType() const;
        const CCUpgrade &       getUpgrade()  const;

        std::vector<UnitType>   whatBuilds;

#ifndef SC2API
        MetaType(const BWAPI::TechType & tech, CCBot & bot);
        const BWAPI::TechType & getTechType() const;
#endif
    };
}