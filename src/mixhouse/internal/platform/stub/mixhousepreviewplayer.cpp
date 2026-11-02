/*
 * MixHouse DJ 2
 */
#include "../../mixhousepreviewplayer.h"

namespace au::mixhouse {
struct MixHousePreviewPlayer::Impl {};

MixHousePreviewPlayer::MixHousePreviewPlayer() = default;
MixHousePreviewPlayer::~MixHousePreviewPlayer() = default;

bool MixHousePreviewPlayer::load(const std::string&, const std::string&) { return false; }
void MixHousePreviewPlayer::playFrom(double) {}
void MixHousePreviewPlayer::stop() {}
bool MixHousePreviewPlayer::isPlaying() { return false; }
double MixHousePreviewPlayer::position() { return 0.0; }
}
