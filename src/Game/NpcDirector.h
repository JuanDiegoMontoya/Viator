#pragma once
#include "glm/fwd.hpp"
#include "glm/vec3.hpp"

#include "entt/entity/entity.hpp"

#include <array>
#include <expected>
#include <unordered_map>
#include <vector>

class World;

namespace Game2
{
  struct Prism
  {
    glm::ivec3 min;
    glm::ivec3 extent{};

    bool operator==(const Prism&) const = default;
  };

  struct HousingParams
  {
    int minWidth = 3;
    int maxWidth = 10;

    int minHeight = 2;
    int maxHeight = 5;
  };

  // Houses are not necessarily valid at all times.
  // Vacant houses need only be re-checked for validity when an NPC tries to move in 
  // or when the player explicitly checks if a house is valid. That way we can avoid
  // unnecessary work checking those houses periodically.
  // Occupied houses, however, must be re-checked regularly, lest an NPC live in an 
  // HOA-unapproved home. Some slop is tolerable, so this check only needs to occur
  // occasionally.
  struct House
  {
    Prism interiorVolume;
    entt::entity occupant = entt::null;
  };

  enum class InvalidHousingReason : uint32_t
  {
    RoomReachesWorldEdge,
    RoomIsTooLarge,
    RoomIsTooSmall,
    RoomHasInvalidDimensions,
    RoomIntersectsExistingRoom,
    RoomDoesNotContainBed,
  };

  enum class NpcId : uint32_t
  {
    Merchant,
    COUNT,
  };

  enum class NpcStatus
  {
    // World conditions to allow this NPC to spawn have not yet been met.
    // For most NPCs, their state should never return to this (i.e. they are 'unlocked' forever).
    InitialRequirementsNotMet,

    // Requirements are met, NPC is not alive for one reason or another.
    // Periodic checks will be made to automatically spawn and assign this NPC
    // to an available house.
    NotPresent,

    // Requirements are met, NPC is alive and assigned to a house.
    Present,
  };

  class NpcDirector
  {
  public:
    explicit NpcDirector();
    void Update(World& world, float dt);

    [[nodiscard]] static std::expected<Prism, InvalidHousingReason> CheckIsValidHousing(World& world, glm::ivec3 originalPos, const HousingParams& params);
    [[nodiscard]] std::expected<Prism, InvalidHousingReason> CheckIsValidHousingAndRegister(World& world, glm::ivec3 originalPos, const HousingParams& params);

    std::vector<House> houses;
    std::unordered_map<NpcId, NpcStatus> npcStatuses;
    float validateHousingAccumulator = 0;
    float validateHousingInterval    = 5;
    float bedCheckAccumulator        = 1;
    float bedCheckInterval           = 5;
    float houseCheckAccumulator      = 2;
    float houseCheckInterval         = 5;
    float npcStatusCheckAccumulator  = 3;
    float npcStatusCheckInterval     = 5;
    float previousTime               = 1;
    float trySpawnNpcTime            = 0.25f; // 0.25 = 6am

  private:
    void ValidateRandomOccupiedHouse(World& world);
    void CheckIfRandomBedIsPartOfHouse(World& world);
    void CheckIfRandomHouseContainsBed(World& world);
    [[nodiscard]] bool DoesHouseContainBed(World& world, size_t bedIndex) const;
    void UpdateNpcStatuses(World& world);
    [[nodiscard]] static bool CheckNpcInitialRequirements(World& world, NpcId id);
    void TrySpawnNpcs(World& world);
    [[nodiscard]] entt::entity TrySpawnNpc(World& world, NpcId id);
  };
}