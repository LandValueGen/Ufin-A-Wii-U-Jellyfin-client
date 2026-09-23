// Item presentation: names, tags and detail lines for each Jellyfin
// item type, and which items are playable / Live TV.
#include "check.h"
#include "item_labels.h"

static JellyfinItem make(const std::string& type) {
    JellyfinItem i;
    i.id = "x";
    i.name = "Name";
    i.type = type;
    return i;
}

int main() {
    JellyfinItem lib = make("CollectionFolder");
    lib.collectionType = "movies";
    CHECK_STR(itemTag(lib), "Movies");
    CHECK(!isPlayableItem(lib));
    CHECK(!isLiveTvView(lib));

    JellyfinItem live = make("UserView");
    live.collectionType = "livetv";
    CHECK(isLiveTvView(live));
    CHECK_STR(itemTag(live), "Live TV");

    JellyfinItem movie = make("Movie");
    movie.productionYear = 2008;
    movie.runTimeTicks = 6720LL * 10000000LL; // 1:52:00
    CHECK(isPlayableItem(movie));
    CHECK_STR(itemTag(movie), "2008  1:52:00");
    CHECK_STR(itemDetail(movie), "Name  -  2008  -  1:52:00");

    JellyfinItem ep = make("Episode");
    ep.parentIndexNumber = 2;
    ep.indexNumber = 5;
    ep.runTimeTicks = 2520LL * 10000000LL; // 42:00
    CHECK(isPlayableItem(ep));
    CHECK_STR(itemTag(ep), "S2E5  42:00");
    CHECK(itemDetail(ep).find("Season 2, Episode 5") != std::string::npos);
    ep.seriesName = "Lost";
    CHECK(itemDetail(ep).find("Lost: Name") == 0); // search results mix shows

    JellyfinItem ch = make("TvChannel");
    ch.channelNumber = "5";
    ch.currentProgram = "News";
    CHECK(isPlayableItem(ch));
    CHECK_STR(itemDisplayName(ch), "5  Name");
    CHECK_STR(itemTag(ch), "LIVE");
    CHECK(itemDetail(ch).find("Now: News") == 0);
    ch.currentProgram.clear();
    CHECK(itemDetail(ch).find("Live channel") == 0);

    JellyfinItem track = make("Audio");
    track.indexNumber = 3;
    track.runTimeTicks = 185LL * 10000000LL;
    CHECK_STR(itemDisplayName(track), "3. Name");
    CHECK_STR(itemTag(track), "3:05");

    CHECK_STR(itemTag(make("Series")), "Series");
    CHECK(!isPlayableItem(make("Series")));
    CHECK_STR(itemTag(make("MusicAlbum")), "Album");
    CHECK_STR(itemTag(make("BoxSet")), "Collection");
    CHECK_STR(itemTag(make("Something")), "Something");

    return check::finish("test_item_labels");
}
