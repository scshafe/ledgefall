// The game server: netplay-kit's server hosting ledgefall's core. Configuration is from the
// environment (see npk/gameserver/main.hpp); the lobby passes each room's options to the factory.
#include "ledgefall/netplay.hpp"
#include "npk/gameserver/main.hpp"

int main() {
  return npk::gameserver::run([](std::size_t players, const npk::json::Value& options) -> std::unique_ptr<npk::session::Simulation> {
    return ledgefall::make_netplay_simulation(players, options);
  });
}
