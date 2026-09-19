// Every user-visible sentence the firmware composes, in one place.
//
// The voice, from the design system: a full sentence in sentence case with a full stop, eight
// words or fewer, "board" and never "device", no exclamation marks, a button is a verb, and when
// the board does not know something it says so. A string that carries a value (a time, a
// percentage, an SSID, a version) is a prefix here with a small helper beside it, so the wording
// is still readable in one place rather than spelled out in a YAML lambda.
//
// What is not here: anything the backend composes (a departure's status, the document's notice),
// the day and month names the clock builds a date from, and the short capitalised labels the
// design sets in mono, which are labels rather than sentences. Nothing here is logged either:
// a log line carries the error code the sentence on the wall leaves out.
//
// firmware/sim/tools/check_glyphs.py reads this file, so every character shipped in a string is
// checked against the compiled fonts.
#pragma once

#include <string>

namespace hb {
namespace copy {

// ---------------------------------------------------------------- stamps and card labels
// Mono, capitals, tracked: the label at the top of an empty card. Not sentences, so no full stops.
inline constexpr const char *NOTHING_DUE = "NOTHING DUE";
inline constexpr const char *OFFLINE = "OFFLINE";
inline constexpr const char *NOTHING_PLANNED = "NOTHING PLANNED";
inline constexpr const char *ALL_DONE = "ALL DONE";
// The weather strip's left label when the document named no place.
inline constexpr const char *WEATHER = "WEATHER";
// The diary strip's right half, and the heading the day after today gets.
inline constexpr const char *TODO_LIST = "TO-DO LIST";
inline constexpr const char *TOMORROW_HEAD = "TOMORROW";

// ---------------------------------------------------------------- empty states
inline constexpr const char *BOARD_NO_TIMETABLE = "Can't reach the timetable.";
inline constexpr const char *BOARD_NONE_STOP = "Nothing due at this stop.";
inline constexpr const char *BOARD_NONE_ARRIVING = "Nothing arriving in the next two hours.";
inline constexpr const char *BOARD_NONE_DUE = "Nothing due in the next two hours.";
inline constexpr const char *AGENDA_EMPTY = "Nothing in the diary this week. Enjoy it.";
inline constexpr const char *LIST_EMPTY = "Nothing on the list. Enjoy it.";
inline constexpr const char *SKY_NO_FORECAST = "Can't reach the forecast.";

// ---------------------------------------------------------------- rows
// A diary row's second line, and the word in front of a temperature that is not the real one.
inline constexpr const char *ALL_DAY = "All day";
inline constexpr const char *TOMORROW = "Tomorrow";
inline constexpr const char *FEELS = "feels ";

// ---------------------------------------------------------------- the boot screen
// The four steps, each in the shape it takes while it is running and once it is done. A step is
// a line in a checklist rather than a sentence, so none of them ends in a full stop.
inline constexpr const char *BOOT_WIFI = "Connecting to Wi-Fi";
inline constexpr const char *BOOT_WIFI_DONE = "Connected to Wi-Fi";
inline constexpr const char *BOOT_PAGES = "Fetching your pages";
inline constexpr const char *BOOT_PAGES_DONE = "Pages loaded";
inline constexpr const char *BOOT_CLOCK = "Setting the clock";
inline constexpr const char *BOOT_CLOCK_DONE = "Clock set";
inline constexpr const char *BOOT_READY = "Ready";
inline constexpr const char *BOOT_PAIR = "Pair this board";
// The help line under the steps, which says what the household should do about it.
inline constexpr const char *BOOT_WIFI_HELP = "Still looking for Wi-Fi. Hold the side button.";
inline constexpr const char *BOOT_BACKEND_HELP = "Can't reach HallBoard. Trying again.";

// ---------------------------------------------------------------- the clock and the pairing page
inline constexpr const char *CLOCK_NOT_SET = "The clock is not set yet.";
inline constexpr const char *CLOCK_WAITING = "Setting the clock.";
inline constexpr const char *PAIR_SCAN = "Scan, or type it at hallboard.co.uk/pair.";
inline constexpr const char *PAIR_WAITING = "Getting a pairing code.";

// ---------------------------------------------------------------- what the backend is doing
inline constexpr const char *WIFI_CONNECTING = "Connecting to Wi-Fi.";
inline constexpr const char *WIFI_NONE_SAVED = "No Wi-Fi network saved. Hold the side button.";
inline constexpr const char *WIFI_DROPPED = "Wi-Fi has dropped. Hold the side button.";
inline constexpr const char *CONNECTED_FETCHING = "Connected. Fetching your pages.";
inline constexpr const char *CONNECTED_PAIRING = "Connected. Getting a pairing code.";
inline constexpr const char *PAIRED_FETCHING = "Paired. Fetching your pages.";
inline constexpr const char *PAIR_FAILED = "Pairing didn't work. Trying again.";
inline constexpr const char *CANT_REACH = "Can't reach HallBoard.";
inline constexpr const char *BACKEND_ERROR = "HallBoard sent an error.";
inline constexpr const char *CANT_READ_PAGES = "Couldn't read the pages.";
inline constexpr const char *RATE_LIMITED = "Asked too often. Trying again shortly.";
inline constexpr const char *FORGETTING = "Forgetting everything and restarting.";

// ---------------------------------------------------------------- firmware updates
inline constexpr const char *UPDATING_TO = "Updating to ";       // + version
inline constexpr const char *UPDATING_BOARD = "Updating the board";
inline constexpr const char *UPDATED_TO = "Updated to ";         // + version + "."
inline constexpr const char *UPDATE_FAILED = "Update failed. Trying again later.";

// ---------------------------------------------------------------- the Wi-Fi setup pages
inline constexpr const char *WIFI_SCANNING = "Looking for networks.";
inline constexpr const char *WIFI_SCAN_FAILED = "Scan failed. Tap Scan again.";
inline constexpr const char *WIFI_NO_NETWORKS = "No networks found. Tap Scan again.";
inline constexpr const char *WIFI_PICK = "Tap your network.";
inline constexpr const char *WIFI_NOW_ON = " Now on ";           // + ssid + "."
inline constexpr const char *WIFI_PASSWORD_FOR = "Password for ";  // + ssid
inline constexpr const char *WIFI_CONNECTING_TO = "Connecting to ";  // + ssid + "."
inline constexpr const char *WIFI_CONNECTED_TO = "Connected to ";    // + ssid + "."

// ---------------------------------------------------------------- the detail sheet
inline constexpr const char *DETAIL_LOADING = "Loading the calling points.";
inline constexpr const char *DETAIL_FAILED = "Can't load the calling points.";
inline constexpr const char *DETAIL_EMPTY = "No calling points to show.";

// ---------------------------------------------------------------- the helpers
// A sentence about not reaching the backend, with the time of what is still on screen after it:
// "Can't reach HallBoard. Showing 08:12." Before the first good fetch, or before the clock is
// set, there is no time to name and the sentence stands on its own rather than saying "--:--".
inline std::string with_showing(const char *sentence, const std::string &hm) {
  std::string s(sentence);
  if (!hm.empty()) s += " Showing " + hm + ".";
  return s;
}

// "Updating to 1.4.1, 42%." The version is the one the backend offered; without it the board
// still says what it is doing, because a percentage on its own says nothing.
inline std::string updating(const std::string &version, int pct) {
  std::string s = version.empty() ? std::string(UPDATING_BOARD) : UPDATING_TO + version;
  return s + ", " + std::to_string(pct) + "%.";
}

// How long a diary event has left, as the raised row says it. Minutes, rounded to hours past the
// hour, because a wall board is read from across a room and not to the minute.
inline std::string starts_in(int minutes) {
  if (minutes <= 0) return "now";
  if (minutes == 1) return "in 1 minute";
  if (minutes < 60) return "in " + std::to_string(minutes) + " minutes";
  int h = (minutes + 30) / 60;
  return h <= 1 ? "in 1 hour" : "in " + std::to_string(h) + " hours";
}

}  // namespace copy
}  // namespace hb
