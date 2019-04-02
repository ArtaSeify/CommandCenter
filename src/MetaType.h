#pragma once

#include "Common.h"
#include "UnitType.h"
#include "AbilityAction.h"

namespace CC
{
    namespace MetaTypes
    {
        enum { Unit, Upgrade, Buff, Tech, Ability, None };
    }

    struct AbilityAction
    {
        CCUnitID        target_id;
        UnitType        target_type;
        sc2::AbilityID  targetProduction_ability;
        std::string     targetProduction_name;

        AbilityAction() : target_id(), target_type(), targetProduction_ability() {}
    };

    using AbilityType = std::pair<CCAbility, AbilityAction>;
    
    class CCBot;
    class MetaType
    {
    private:
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
        MetaType(const std::string & name, const AbilityAction & abilityInfo, CCBot & bot);
        MetaType(const UnitType & unitType, CCBot & bot);
        MetaType(const CCUpgrade & upgradeType, CCBot & bot);
        MetaType(const CCAbility & abilityType, const AbilityAction & abilityInfo, CCBot & bot);

        bool    isUnit()        const;
        bool    isUpgrade()     const;
        bool    isTech()        const;
        bool    isBuilding()    const;
        bool    isAbility()     const;

        const size_t &          getMetaType()  const;
        const std::string &     getName()       const;
        const CCRace &          getRace()       const;
        const UnitType &        getUnitType() const;
        const CCUpgrade &       getUpgrade()  const;
        const AbilityType &     getAbility()  const;

        std::vector<UnitType>   whatBuilds;

#ifndef SC2API
        MetaType(const BWAPI::TechType & tech, CCBot & bot);
        const BWAPI::TechType & getTechType() const;
#endif
    };
}