// session.cpp
// Client session implementation

#include "session/session.h"

namespace hb::session {

session::session(session_id id, connection_id connection)
    : id_(id)
    , connection_(connection)
    , connected_at_(std::chrono::steady_clock::now())
    , last_activity_(connected_at_)
{
}

void session::set_state(session_state state) {
    state_ = state;
    update_activity();
}

void session::set_account(account_info info) {
    account_ = std::move(info);
    update_activity();
}

void session::set_current_player(hb::player_id id) {
    current_player_ = id;
    update_activity();
}

void session::set_characters(std::vector<character_info> chars) {
    characters_ = std::move(chars);
    update_activity();
}

void session::update_activity() {
    last_activity_ = std::chrono::steady_clock::now();
}

}  // namespace hb::session
