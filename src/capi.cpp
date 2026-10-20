#include "ledgefall/capi.h"

#include <new>
#include <vector>

#include "ledgefall/bot.hpp"
#include "internal.hpp"
#include "ledgefall/match.hpp"

struct lf_match {
  ledgefall::Rules rules;
  ledgefall::State state;
  std::vector<ledgefall::Bot> bots;
  std::vector<int> is_bot;
  std::vector<ledgefall::Input> inputs;  // scratch, one per seat
};

namespace {

using namespace ledgefall;

bool seat_ok(const lf_match* m, int seat) { return m != nullptr && seat >= 0 && seat < static_cast<int>(m->state.players.size()); }

}  // namespace

extern "C" {

lf_match* lf_match_create(int players, int stocks) { return lf_match_create_with(players, stocks, 1, nullptr, LF_STAGE_PRACTICE); }

lf_match* lf_match_create_with(int players, int stocks, uint32_t seed, const uint8_t* characters, int stage) {
  lf_setup setup{};
  setup.players = players;
  setup.stocks = stocks;
  setup.seed = seed;
  setup.stage = stage;
  for (int i = 0; i < 4; ++i) {
    setup.teams[i] = -1;
    // Seats without a character of their own get Mason, Glim, Pip and Fuse in turn.
    setup.characters[i] = characters != nullptr && i < players ? characters[i] : static_cast<uint8_t>(i);
  }
  return lf_match_create_setup(&setup);
}

lf_match* lf_match_create_setup(const lf_setup* in) {
  if (in == nullptr || in->stage < 0 || in->stage >= LF_STAGE_COUNT) return nullptr;
  Setup setup;
  setup.players = in->players;
  setup.stocks = in->stocks;
  setup.seed = in->seed;
  setup.stage = static_cast<StageId>(in->stage);
  for (std::size_t i = 0; i < 4; ++i) {
    setup.characters[i] = static_cast<Character>(in->characters[i]);
    setup.teams[i] = in->teams[i];
    setup.seat_stocks[i] = in->seat_stocks[i];
  }
  auto rules = ledgefall::make_rules(setup);
  if (!rules) return nullptr;
  lf_match* m = new (std::nothrow) lf_match;
  if (m == nullptr) return nullptr;
  m->rules = *rules;
  m->state = ledgefall::make_state(m->rules);
  m->bots.assign(static_cast<std::size_t>(in->players), {});
  m->is_bot.assign(static_cast<std::size_t>(in->players), 0);
  m->inputs.assign(static_cast<std::size_t>(in->players), {});
  return m;
}

void lf_match_destroy(lf_match* m) { delete m; }

int lf_match_set_bot(lf_match* m, int seat, int level) {
  if (!seat_ok(m, seat) || level < LF_BOT_NONE || level > LF_BOT_HARD) return 0;
  const auto i = static_cast<std::size_t>(seat);
  m->is_bot[i] = level != LF_BOT_NONE;
  m->bots[i] = {};
  if (level != LF_BOT_NONE) m->bots[i].level = static_cast<BotLevel>(level);
  return 1;
}

void lf_match_step(lf_match* m, const lf_input* inputs) {
  if (m == nullptr) return;
  for (std::size_t i = 0; i < m->inputs.size(); ++i) {
    if (m->is_bot[i] != 0) {
      m->inputs[i] = bot_input(m->rules, m->state, static_cast<int>(i), m->bots[i]);
    } else if (inputs != nullptr) {
      // The host's bytes are not trusted: clamp to what the core accepts.
      int axis = inputs[i].move_x;
      if (axis < -kAxisMax) axis = -kAxisMax;
      if (axis > kAxisMax) axis = kAxisMax;
      m->inputs[i] = {static_cast<std::int8_t>(axis), static_cast<std::uint8_t>(inputs[i].buttons & kButtonMask)};
    } else {
      m->inputs[i] = {};
    }
  }
  ledgefall::step(m->rules, m->state, m->inputs);
}

int lf_match_players(const lf_match* m) { return m ? static_cast<int>(m->state.players.size()) : 0; }
uint32_t lf_match_tick(const lf_match* m) { return m ? m->state.world.frame : 0; }
int lf_match_over(const lf_match* m) { return m && m->state.over ? 1 : 0; }
int lf_match_winner(const lf_match* m) { return m && m->state.over ? m->state.winner : -1; }
int lf_match_winning_team(const lf_match* m) { return m ? ledgefall::winning_team(m->rules, m->state) : -1; }
uint64_t lf_match_hash(const lf_match* m) { return m ? ledgefall::hash(m->state) : 0; }

int lf_match_player(const lf_match* m, int seat, lf_player* out) {
  if (!seat_ok(m, seat) || out == nullptr) return 0;
  const PlayerState& p = m->state.players[static_cast<std::size_t>(seat)];
  out->stocks = p.stocks;
  out->respawn_timer = p.respawn_timer;
  out->character = static_cast<int32_t>(m->rules.setup.characters[static_cast<std::size_t>(seat)]);
  for (int i = 0; i < 3; ++i) out->cooldown[i] = p.cooldown[static_cast<std::size_t>(i)];
  out->place = p.place;
  out->team = m->rules.setup.teams[static_cast<std::size_t>(seat)];
  out->kos = p.stats.kos;
  out->falls = p.stats.falls;
  return 1;
}

int lf_match_entity(const lf_match* m, int seat, lf_entity* out) {
  if (!seat_ok(m, seat) || out == nullptr) return 0;
  const auto i = static_cast<std::size_t>(seat);
  const fp::EntityState& e = m->state.world.entities[i];
  const fp::MovementParams& p = m->rules.engine.params[i];
  out->x = e.pos.x.raw();
  out->y = e.pos.y.raw();
  out->vx = e.vel.x.raw();
  out->vy = e.vel.y.raw();
  out->half_width = p.body_half_width.raw();
  out->height = p.body_height.raw();
  out->facing = e.facing;
  out->mode = static_cast<std::uint8_t>(e.mode);
  out->eliminated = e.eliminated;
  out->grounded = e.mode != fp::Mode::Airborne ? 1 : 0;
  return 1;
}

int lf_match_platform_count(const lf_match* m) {
  return m ? static_cast<int>(m->rules.engine.stage.platforms.size()) : 0;
}

int lf_match_platform(const lf_match* m, int index, lf_platform* out) {
  if (m == nullptr || out == nullptr || index < 0 || index >= lf_match_platform_count(m)) return 0;
  const auto i = static_cast<std::size_t>(index);
  const fp::PlatformDef& d = m->rules.engine.stage.platforms[i];
  const fp::PlatformState& s = m->state.world.platforms[i];
  out->left = (d.rect.left + s.offset.x).raw();
  out->right = (d.rect.right + s.offset.x).raw();
  out->bottom = (d.rect.bottom + s.offset.y).raw();
  out->top = (d.rect.top + s.offset.y).raw();
  out->phase = static_cast<std::uint8_t>(s.phase);
  out->timer = s.timer;
  out->sturdy = m->rules.slab_hp[i] != 0 ? 1 : 0;
  out->hp = m->state.slab_hp[i];
  out->stress = s.stress.raw();
  return 1;
}

int lf_match_thing_count(const lf_match* m) {
  if (m == nullptr) return 0;
  int n = 0;
  for (const fp::Placeable& p : m->state.world.placeables) n += p.active != 0 ? 1 : 0;
  for (const Object& o : m->state.objects) n += o.kind != ObjectKind::None ? 1 : 0;
  for (const HazardItem& h : m->state.hazards.list) n += h.kind != HazardKind::None ? 1 : 0;
  return n;
}

int lf_match_thing(const lf_match* m, int index, lf_thing* out) {
  if (m == nullptr || out == nullptr || index < 0) return 0;
  int at = 0;
  for (std::size_t slot = 0; slot < m->state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = m->state.world.placeables[slot];
    if (p.active == 0 || at++ != index) continue;
    out->kind = p.kind == fp::PlaceableKind::Wall ? LF_THING_WALL
                : p.kind == fp::PlaceableKind::Spring ? LF_THING_SPRING
                : p.kind == fp::PlaceableKind::Sticky ? (m->state.meta[slot].tag == 1 ? LF_THING_THORNS : LF_THING_GLUE)
                                                      : LF_THING_OIL;
    out->left = p.rect.left.raw();
    out->right = p.rect.right.raw();
    out->bottom = p.rect.bottom.raw();
    out->top = p.rect.top.raw();
    out->owner = m->state.meta[slot].owner;
    out->armed = 1;
    return 1;
  }
  for (const Object& o : m->state.objects) {
    if (o.kind == ObjectKind::None || at++ != index) continue;
    const fp::Rect r = detail::object_rect(o);
    out->kind = o.kind == ObjectKind::Lob ? LF_THING_LOB
                : o.kind == ObjectKind::Acid ? LF_THING_ACID
                : o.kind == ObjectKind::Gust ? LF_THING_GUST
                : o.kind == ObjectKind::Anvil ? LF_THING_ANVIL
                : o.kind == ObjectKind::Bumper ? LF_THING_BUMPER
                : o.kind == ObjectKind::Charge ? LF_THING_CHARGE
                : o.kind == ObjectKind::Saw ? LF_THING_SAW
                : o.kind == ObjectKind::Roots ? LF_THING_ROOTS
                : o.kind == ObjectKind::Rub ? LF_THING_RUB : LF_THING_MAGNET;
    out->left = r.left.raw();
    out->right = r.right.raw();
    out->bottom = r.bottom.raw();
    out->top = r.top.raw();
    out->owner = o.owner;
    out->armed = o.arm == 0 ? 1 : 0;
    return 1;
  }
  for (const HazardItem& h : m->state.hazards.list) {
    if (h.kind == HazardKind::None || at++ != index) continue;
    const fp::Fixed w = detail::fx100(h.kind == HazardKind::Rock || h.kind == HazardKind::RockWarn ? 960 : 832);
    // A comet's warning is a stripe across the lane; a rock's, a column down from the top.
    const fp::Rect& b = m->rules.engine.stage.blast;
    fp::Rect r{h.pos.x - w, h.pos.x + w, h.pos.y - w, h.pos.y + w};
    if (h.kind == HazardKind::CometWarn) r = {b.left, b.right, h.pos.y - detail::fx100(100), h.pos.y + detail::fx100(100)};
    if (h.kind == HazardKind::RockWarn) r = {h.pos.x - w, h.pos.x + w, b.bottom, b.top};
    out->kind = h.kind == HazardKind::Comet ? LF_THING_COMET : h.kind == HazardKind::Rock ? LF_THING_ROCK
                : h.kind == HazardKind::CometWarn ? LF_THING_COMET_WARN : LF_THING_ROCK_WARN;
    out->left = r.left.raw();
    out->right = r.right.raw();
    out->bottom = r.bottom.raw();
    out->top = r.top.raw();
    out->owner = h.redirected_by;
    out->armed = 1;
    return 1;
  }
  return 0;
}

void lf_match_weather(const lf_match* m, lf_weather* out) {
  if (m == nullptr || out == nullptr) return;
  const Hazards& h = m->state.hazards;
  out->tide_phase = static_cast<int32_t>(h.tide);
  out->tide_level = h.tide_level.raw();
  out->gale_phase = static_cast<int32_t>(h.gale);
  out->gale_dir = h.gale_dir;
}

void lf_match_blast(const lf_match* m, int32_t out[4]) {
  if (m == nullptr || out == nullptr) return;
  const fp::Rect& b = m->rules.engine.stage.blast;
  out[0] = b.left.raw();
  out[1] = b.right.raw();
  out[2] = b.bottom.raw();
  out[3] = b.top.raw();
}

}  // extern "C"
