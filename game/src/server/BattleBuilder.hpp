//
// Created by ADMIN on 18-May-24.
//

#ifndef MOTION_BATTLEBUILDER_HPP
#define MOTION_BATTLEBUILDER_HPP


#include <optional>
#include <numeric>
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include "LobbyConfig.hpp"
#include "EntityComponentSystem.hpp"
#include "Unit.hpp"
#include "../Player.hpp"
#include <SFML/Graphics.hpp>
#include <glm/vec4.hpp>
#include <queue>

#include <iostream>

struct LocationComponent {
  typedef glm::vec2 location_t;
  location_t location;
};

struct MovementComponent {
  typedef float speed_t;
  typedef float direction_t;
  speed_t speed;
  direction_t direction;
};

struct HealthComponent {
  typedef int health_t;
  health_t health;
};

struct AttackComponent {
//  typedef int attack_t;
//  attack_t attack;
};

struct DirectionComponent {
  typedef glm::vec2 direction_t;
  direction_t direction;
};

struct SpeedComponent {
  typedef float speed_t;
  speed_t speed;
};

struct DestinationComponent {
  typedef glm::vec2 destination_t;
  destination_t destination;
};

struct UnitStatsComponent {
  UNIT_TEMPLATE unit_template;

  [[nodiscard]] const UnitStats &get_stats() const {
    return get_unit_stats(unit_template);
  }
};

struct OwnerComponent {
  Player owner;
};

byte_istream &operator>>(byte_istream &istream, OwnerComponent &ownerComponent);

byte_ostream &operator<<(byte_ostream &ostream, const OwnerComponent &ownerComponent);

inline double vector_to_angle(glm::vec2 vector) {
  return std::atan2(vector.y, vector.x);
}
class UnitComponentSystem : public serializable {
  ComponentGroup<HealthComponent, UnitStatsComponent, OwnerComponent, LocationComponent, DirectionComponent, DestinationComponent, SpeedComponent> mainComponentGroup;
//  ComponentGroup<HealthComponent, UnitStatsComponent, OwnerComponent> existanceComponentGroup;
  ComponentGroup<AttackComponent, UnitStatsComponent, OwnerComponent> attackComponentGroup;

  double time = 0;
  typedef double expiry_time_t;
//  double expiry_time = 0;
  std::priority_queue<std::tuple<expiry_time_t, UnitId>> on_cooldown{};

  void update_movement(float delta_time) {

    auto speed_begin = mainComponentGroup.get<SpeedComponent>().begin();
    auto speed_end = mainComponentGroup.get<SpeedComponent>().end();
    auto destination_begin = mainComponentGroup.get<DestinationComponent>().begin();
    auto direction_begin = mainComponentGroup.get<DirectionComponent>().begin();
    auto location_begin = mainComponentGroup.get<LocationComponent>().begin();
    std::vector<UnitId> to_stop;
    for (std::size_t i = 0;
         speed_begin != speed_end; ++i, ++speed_begin, ++direction_begin, ++location_begin, ++destination_begin) {
      // would be nice to stop the motion when close to destination
      auto distance_to_dest = glm::distance(location_begin->location, destination_begin->destination);
      auto distance_to_move = speed_begin->speed * delta_time;
      if (distance_to_dest < distance_to_move) {
        location_begin->location = destination_begin->destination;
        to_stop.push_back(mainComponentGroup.get<SpeedComponent>().get_dense()[i]);
        continue;
      }
      location_begin->location += distance_to_move * direction_begin->direction;
    }
    for (auto id: to_stop) {
      mainComponentGroup.erase<SpeedComponent>(id);
      mainComponentGroup.erase<DestinationComponent>(id);
    }
  }
  void update_cool_down(float delta_time) {
    // ignore dead
    while (!on_cooldown.empty() && std::get<expiry_time_t>(on_cooldown.top()) > - time) {
      auto [_, id] = on_cooldown.top();
      on_cooldown.pop();
//      on_cooldown.

      auto & sparse = attackComponentGroup.get<UnitStatsComponent>().get_sparse();
      auto & dense = attackComponentGroup.get<UnitStatsComponent>().get_dense();

      if (sparse[id] < dense.size()) {
        attackComponentGroup.insert(id, AttackComponent{});
      }


    }
  }
  void update_attack(float delta_time) {
    auto attack_begin = attackComponentGroup.get<AttackComponent>().begin();
    auto dense_begin = attackComponentGroup.get<AttackComponent>().get_dense().begin();
    auto stats_begin = attackComponentGroup.get<UnitStatsComponent>().begin();
    auto owner_begin = attackComponentGroup.get<OwnerComponent>().begin();
    auto attack_end = attackComponentGroup.get<AttackComponent>().end();
    std::vector<UnitId> to_kill;
    std::vector<UnitId> attacked;
    for (; attack_begin != attack_end; ++attack_begin, ++stats_begin, ++owner_begin, ++dense_begin) {

      // check if the unit is in range
      // if it is, attack
      // if it is not, move towards the target
      auto id = *dense_begin;
      auto & this_location = mainComponentGroup.get<LocationComponent>()[id].location;
      const auto & stats = stats_begin->get_stats();

      auto location_begin = mainComponentGroup.get<LocationComponent>().begin();
      auto other_owner_begin = mainComponentGroup.get<OwnerComponent>().begin();
      auto other_health_begin = mainComponentGroup.get<HealthComponent>().begin();
      auto other_dense_begin = mainComponentGroup.get<LocationComponent>().get_dense().begin();
      auto location_end = mainComponentGroup.get<LocationComponent>().end();

      for (; location_begin != location_end; ++location_begin, ++other_owner_begin, ++other_dense_begin, ++other_health_begin) {
        if (owner_begin->owner.getID() == other_owner_begin->owner.getID()) {
          continue;
        }
        if ( other_health_begin->health <= 0) {
          continue;
        }
        auto & other_location = location_begin->location;
        auto distance = glm::distance(this_location, other_location);
        if (distance < stats.range) {
          // attack
          if (time > stats.attack_cool_down) {
            on_cooldown.emplace( - (time + stats.attack_cool_down), id);
            other_health_begin->health -= stats.attack;
//            std::cout << "Unit " << id << " attacked unit " << *other_dense_begin << "Final health: " << other_health_begin->health   << std::endl;
            attacked.push_back(id);
            // iterator may be invalidated if erased now
            if (other_health_begin->health <= 0) {
              to_kill.push_back(*other_dense_begin);
            }
          }
          break;
        } else {
          if (distance < 10 * stats.range) {
            set_destination(id,  other_location);
          }
          else if (distance < 50 * stats.range) {
            set_destination(id,  (other_location +  this_location) / 2.f);
          }


          break;
        }
      }


//      auto location = mainComponentGroup.get<LocationComponent>()[attack_begin];
    }

    for (auto id: attacked) {
      attackComponentGroup.erase<AttackComponent>(id);
    }
    std::size_t units_dead = 0;

    for (auto id: to_kill) {
      mainComponentGroup.erase_if_present<SpeedComponent>(id);
      mainComponentGroup.erase_if_present<DestinationComponent>(id);
      mainComponentGroup.erase_if_present<DirectionComponent>(id);
      mainComponentGroup.erase_if_present<UnitStatsComponent>(id);
      mainComponentGroup.erase_if_present<LocationComponent>(id);
      mainComponentGroup.erase_if_present<HealthComponent>(id);
      mainComponentGroup.erase_if_present<OwnerComponent>(id);
      mainComponentGroup.erase_if_present<HealthComponent>(id);
//      std::cout << "Unit " << id << " died" << std::endl;
//      attackComponentGroup.erase<UnitStatsComponent>(id);
//      attackComponentGroup.erase<UnitStatsComponent>(id);
      // how to remove from cooldown??
      attackComponentGroup.erase_if_present<AttackComponent>(id);
      attackComponentGroup.erase_if_present<UnitStatsComponent>(id);
      attackComponentGroup.erase_if_present<OwnerComponent>(id);
    }
    static std::size_t dead = 0;
    units_dead = to_kill.size();
    if (units_dead > 0)
      dead += units_dead;
      std::cout << "Total units dead: " << dead << std::endl;


  }

public:
  UnitComponentSystem(const std::unordered_map<Player, UnitConfig> &unitConfig, std::size_t universe_size) :
      mainComponentGroup(universe_size),
//      mainComponentGroup(universe_size),
      attackComponentGroup(universe_size) {
//    std::size_t universe_size = std::accumulate(std::begin(unitConfig), std::end(unitConfig), 0, [](auto acc, const auto &pair) {
//      return acc + pair.second;
//    });



    UnitId id = 0;
    std::size_t player_count = 0;
    std::size_t player_slot = 0;
    for (const auto &[player, config]: unitConfig) {
      std::size_t unit_group = 0;

      if (player_count != 0 && player_count % 2 == 0) {
        player_slot += 1;
      }
      float army_offset_x = (config.size()) * player_slot * 20 * 2;
      float army_offset_y = 120 * (player_count % 2);
      for (const auto &[unit_template, count]: config) {

        std::size_t row_count = 0;
        std::size_t units_in_a_row = (count / 32) + 1;
        auto group_offset = static_cast<double>(unit_group * (32 + 1));

        for (std::size_t i = 0; i < count; ++i) {
          auto current_unit = i % units_in_a_row;
          if  (current_unit == 0) {
            ++row_count;
          }
          mainComponentGroup.get<LocationComponent>().insert(id, LocationComponent{{army_offset_x+ group_offset + static_cast<double>(row_count),army_offset_y + static_cast<double>(current_unit) * 0.5}});

          mainComponentGroup.get<DirectionComponent>().insert(id, DirectionComponent{{1, 0}});
          mainComponentGroup.get<HealthComponent>().insert(id, HealthComponent{get_unit_stats(unit_template).health});
          mainComponentGroup.get<UnitStatsComponent>().insert(id, UnitStatsComponent{unit_template});
          mainComponentGroup.get<OwnerComponent>().insert(id, OwnerComponent{player}); //could be opitimised to use a range of ids
          attackComponentGroup.get<AttackComponent>().insert(id, AttackComponent{});
          attackComponentGroup.get<OwnerComponent>().insert(id, OwnerComponent{player});
          attackComponentGroup.get<UnitStatsComponent>().insert(id, UnitStatsComponent{unit_template});
          ++id;

        }
        ++unit_group;



      }
      ++player_count;
    }
  }
  void move_selected_units(const std::vector<UnitId> &units, glm::vec2 destination) {



    std::size_t row_count = 0;
    std::size_t units_in_a_row = units.size() / 32;
//    destination.x -= std::min(32ul, units.size()) / 4;
    float initial_x = destination.x;
    float initial_y = destination.y;
    std::size_t unit_count = 0;
    for (auto unit: units) {
      auto current_unit = unit_count % 32;
      if  (current_unit == 0) {
        ++row_count;
        destination.x = initial_x;
      }
      auto & unit_stats = get_unit_stats(mainComponentGroup.get<UnitStatsComponent>()[unit].unit_template);
      destination.x = initial_x + 2.5f * unit_stats.size * static_cast<float>(current_unit);
      destination.y = initial_y + 4.f * static_cast<float>(row_count) * unit_stats.size;

      set_destination(unit, destination);
      ++unit_count;
    }
  }

  std::vector<UnitId> select_units_in(glm::vec4 &bbox, const Player &player) {
    std::vector<UnitId> selected;
    auto location_begin = mainComponentGroup.get<LocationComponent>().begin();
    auto location_end = mainComponentGroup.get<LocationComponent>().end();
    auto owner_begin = mainComponentGroup.get<OwnerComponent>().begin();
    auto location_dense_begin = mainComponentGroup.get<LocationComponent>().get_dense().begin();
    auto min_x = std::numeric_limits<float>::max();
    auto min_y = std::numeric_limits<float>::max();
    auto max_x = std::numeric_limits<float>::min();
    auto max_y = std::numeric_limits<float>::min();

    for (; location_begin != location_end; ++location_dense_begin, ++location_begin, ++owner_begin) {
      if (owner_begin->owner.getID() != player.getID()) {
        continue;
      }
      if (bbox.x < location_begin->location.x && location_begin->location.x < bbox.z &&
          bbox.y < location_begin->location.y && location_begin->location.y < bbox.w) {
        selected.push_back(*location_dense_begin);
        max_x = std::max(max_x, location_begin->location.x);
        max_y = std::max(max_y, location_begin->location.y);
        min_x = std::min(min_x, location_begin->location.x);
        min_y = std::min(min_y, location_begin->location.y);
      }
    }
    if (selected.size() > 1) {
      bbox.x = min_x;
      bbox.y = min_y;
      bbox.z = max_x;
      bbox.w = max_y;
    }
    return selected;
  }

  void set_destination(UnitId id, glm::vec2 destination) {
    auto direction = glm::normalize(destination - mainComponentGroup.get<LocationComponent>()[id].location);

    const auto &stats = mainComponentGroup.get<UnitStatsComponent>()[id].get_stats();

    mainComponentGroup.insert(id, DirectionComponent{direction});
    mainComponentGroup.insert(id, DestinationComponent{destination});
    mainComponentGroup.insert(id, SpeedComponent{stats.speed}); // no acceleration for now


  }

  UnitId get_unit_id_at(glm::vec2 location) {
    auto location_begin = mainComponentGroup.get<LocationComponent>().begin();
    auto stats_begin = mainComponentGroup.get<UnitStatsComponent>().begin();
    auto location_dense_begin = mainComponentGroup.get<LocationComponent>().get_dense().begin();
    auto location_end = mainComponentGroup.get<LocationComponent>().end();
    for (; location_begin != location_end; ++location_dense_begin, ++location_begin, ++stats_begin) {
      if (glm::distance(location_begin->location, location) < get_unit_stats(stats_begin->unit_template).size) {
        return *location_dense_begin;
      }
    }
    return std::numeric_limits<UnitId>::max();
  }

  void update(float delta_time) {
    time += delta_time;
    update_movement(delta_time);
    update_cool_down(delta_time);
    update_attack(delta_time);
  }

  byte_istream &deserialize(byte_istream &istream) final {
    // parses the whole component
    mainComponentGroup.deserialize(istream);
    mainComponentGroup.deserialize(istream);
    attackComponentGroup.deserialize(istream);
    return istream;
  }

  byte_ostream &serialize(byte_ostream &ostream) const final {
    // serializes the whole component
    mainComponentGroup.serialize(ostream);
    mainComponentGroup.serialize(ostream);
    attackComponentGroup.serialize(ostream);
    return ostream;
  }

  void update_with_remote(byte_istream &istream) {
    // updates only dynamic components
    mainComponentGroup.get<LocationComponent>().deserialize(istream);
    mainComponentGroup.get<DirectionComponent>().deserialize(istream);
    mainComponentGroup.get<HealthComponent>().deserialize(istream);
  }

  void send_to_remote(byte_ostream &ostream) {
    // writes only dynamic components
    mainComponentGroup.get<LocationComponent>().serialize(ostream);
    mainComponentGroup.get<DirectionComponent>().serialize(ostream);
    mainComponentGroup.get<HealthComponent>().serialize(ostream);
  }


  void draw(sf::RenderWindow &win) {
//    auto location_begin = movementComponentGroup.get<LocationComponent>().begin();

//    auto location_end = movementComponentGroup.get<LocationComponent>().end();
    const auto &location_data = mainComponentGroup.get<LocationComponent>();

    auto location_begin = location_data.begin();
    auto location_end = location_data.end();
    auto direction_begin = mainComponentGroup.get<DirectionComponent>().begin();
    auto stats_begin = mainComponentGroup.get<UnitStatsComponent>().begin();
    auto owner_begin = mainComponentGroup.get<OwnerComponent>().begin();
    auto location_begin_dense = location_data.get_dense().begin();
    for (std::size_t i = 0; location_begin != location_end; ++i, ++location_begin, ++location_begin_dense, ++stats_begin, ++direction_begin, ++owner_begin) {
      float size = get_unit_stats(stats_begin->unit_template).size;
      sf::Color color;

      switch (owner_begin->owner.getID()) {
        case 1:
          color = sf::Color::Red;
          break;
        case 2:
          color = sf::Color::Blue;
          break;
        case 3:
          color = sf::Color::Green;
          break;
        case 4:
          color = sf::Color::Yellow;
          break;

      }


      switch (stats_begin->unit_template) {
        case UNIT_TEMPLATE::INFANTRY: {
          sf::CircleShape shape(size);
          shape.setFillColor(sf::Color::White);
          shape.setOutlineColor(color);
          shape.setOutlineThickness(0.2);

          shape.setPosition(location_begin->location.x - size, location_begin->location.y - size);
          win.draw(shape);
          break;
        }
        case UNIT_TEMPLATE::ARMOURED_INFANTRY: {
          sf::RectangleShape shape({size, size});
          shape.setFillColor(sf::Color::White);
          shape.setOutlineColor(color);
          shape.setOutlineThickness(0.2);
          shape.setPosition(location_begin->location.x - size, location_begin->location.y - size);
          win.draw(shape);
          break;
        }
        case UNIT_TEMPLATE::PEASANTS: {
          sf::CircleShape shape(size);
          shape.setFillColor(sf::Color::Cyan);
          shape.setOutlineColor(color);
          shape.setOutlineThickness(0.2);
          shape.setPosition(location_begin->location.x - size, location_begin->location.y - size);
          win.draw(shape);
          break;
        }
        case UNIT_TEMPLATE::SHOCK_INFANTRY: {
          sf::CircleShape shape(size);
          shape.setFillColor(sf::Color::Yellow);
          shape.setOutlineColor(color);
          shape.setOutlineThickness(0.2);
          shape.setPosition(location_begin->location.x - size, location_begin->location.y - size);
          win.draw(shape);
          break;
        }
        case UNIT_TEMPLATE::CAVALRY: {
          sf::RectangleShape shape({2 * size, size / 2});
          shape.setRotation(vector_to_angle(direction_begin->direction) * 180 / M_PI);
          shape.setFillColor(sf::Color::Magenta);
          shape.setOutlineColor(color);
          shape.setOutlineThickness(0.2);
          shape.setPosition(location_begin->location.x - size, location_begin->location.y - size);
          win.draw(shape);
          break;
        }
      }

    }
//    for(auto location: movementComponentGroup.get<LocationComponent>()){
//      sf::CircleShape shape(0.2);
//      shape.setFillColor(sf::Color::Green);
//      shape.setPosition(location.location.x, location.location.y);
//      win.draw(shape);
//    }
  }
};

class Battle {
  TIME time;
  MAP map;
  WEATHER weather;
  UnitComponentSystem unitComponentSystem;
public:
  Battle(TIME time, MAP map, WEATHER weather, const std::unordered_map<Player, UnitConfig> &unitConfig, std::size_t universe_size)
      :
      time(time),
      map(map),
      weather(weather),
      unitComponentSystem(unitConfig, universe_size) {}

  void update(float delta_time) {
    unitComponentSystem.update(delta_time);
  }

  void draw(sf::RenderWindow &win) {

    // draw the battle
    unitComponentSystem.draw(win);
  }

  std::vector<UnitId> select_units_in(glm::vec4 &bbox, const Player &player) {
    return unitComponentSystem.select_units_in(bbox, player);
  }

  void move_selected_units(const std::vector<UnitId> &units, const glm::vec4 &bbox, glm::vec2 destination) {

    unitComponentSystem.move_selected_units(units, destination);
  }


  UnitId get_unit_id_at(glm::vec2 location) {
    return unitComponentSystem.get_unit_id_at(location);
  }

  void move_unit(UnitId id, glm::vec2 destination) {
    unitComponentSystem.set_destination(id, destination);
  }

  void update_with_remote(byte_istream &istream) {
    unitComponentSystem.update_with_remote(istream);
  }

  void send_to_remote(byte_ostream &ostream) {
    unitComponentSystem.send_to_remote(ostream);
  }

  byte_istream &deserialize(byte_istream &istream) {
    istream >> time;
    istream >> map;
    istream >> weather;
    unitComponentSystem.deserialize(istream);
    return istream;
  }

  byte_ostream &serialize(byte_ostream &ostream) const {
    ostream << time;
    ostream << map;
    ostream << weather;
    unitComponentSystem.serialize(ostream);
    return ostream;
  }

};

class BattleBuilder {

  std::unordered_map<Player, UnitConfig> unitConfig;
  std::size_t universe_size;
  LobbyConfig lobbyConfig;
  std::vector<Player> players;
public:

  void parseLobbyConfig(const LobbyConfig &config) {
    lobbyConfig = config;
  }

  void parsePlayers(const std::vector<Player> &players_) {
    players = players_;
  }

  void parseUnitConfig(const std::unordered_map<Player, UnitConfig> &unitConfig_) {
    // takes subset of players specified by lobby config
    for (const auto &player: players) {
      unitConfig[player] = unitConfig_.at(player);
    }
    std::size_t acc = 0;
    for (const auto &pair: unitConfig) {
      std::cout << "Player " << pair.first.getID() << std::endl;
      for (const auto &[unit_template, count]: pair.second) {
        acc += count;
        std::cout << "Unit " << static_cast<int>(unit_template) << " count: " << count << std::endl;
      }
    }
//    universe_size = std::accumulate(std::begin(unitConfig), std::end(unitConfig), 0, [](auto acc1, const auto &pair) {
//      return acc1 + std::accumulate(std::begin(pair.second), std::end(pair.second), acc1, [](auto acc2, const auto &pair) {
//        return acc2 + pair.second;
//      });
//    });
universe_size = acc;
    std::cout << "Universe size: " << universe_size << std::endl;

  }


  Battle build() {
    return {lobbyConfig.time, lobbyConfig.map, lobbyConfig.weather, unitConfig, universe_size};
  }
};


#endif //MOTION_BATTLEBUILDER_HPP
