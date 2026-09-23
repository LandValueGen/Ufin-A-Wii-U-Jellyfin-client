// How Jellyfin items are presented in the list: display name, the short
// right-aligned tag, and the longer detail line shown for the selection.
// Pure functions over JellyfinItem so they're covered by host tests.
#pragma once
#include "jellyfin_client.h"

#include <string>

// True for things Player can play (video, audio, a Live TV channel)
// rather than folders to browse into.
bool isPlayableItem(const JellyfinItem& item);

// True for the "Live TV" view, whose contents come from
// JellyfinClient::getLiveTvChannels() instead of getItems().
bool isLiveTvView(const JellyfinItem& item);

std::string itemDisplayName(const JellyfinItem& item);
std::string itemTag(const JellyfinItem& item);
std::string itemDetail(const JellyfinItem& item);
