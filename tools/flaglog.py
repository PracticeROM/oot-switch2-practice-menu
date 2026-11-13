"""Name the rows of a flag log and add the NPC and dialogue from the game's event scripts.

    python flaglog.py <flags-NNN.csv | StoryEvents.csv | PlayerStatusChanges.csv> [...] [--lang EUen] [--lines 3]

(StoryEvents.csv keeps the module's own cause column; this tool's goes in cause_from_scripts.)

Flag logging (debug > flag logging; src/ootgz/flaglog.h) writes sd:/sloperinaGZ/flaglog/flags-NNN.csv: one row per
saved flag change, with the date and the names the module knows. This writes <name>-named.csv next to each, with
these columns added (name filled in where the module left it empty):

    name, group, description   the field, from data/gamedata_fields.json (Enum values become choice names)
    cause                      what most likely changed it, in words, from the columns below and the log itself:
                                 "talking: <npc>"               a script's change while Link talked (EvtTalk)
                                 "event: <script>"              a script's change during another event (a cutscene)
                                 "arrival event <e> on entering <map> from <map> (entrance <x>)"
                                                                the event the entrance plays on arrival set it
                                 "entering <map> from <map> ..." no script sets it (or only a guess does) and
                                                                Link entered a map within AREA_WINDOW seconds
                                 "getting <item>"               an IsGet.<item> changed in the same frame
                                 "script <script>"              a script's change outside any event
                                 "the game's code ..."          none of these
                               area rows say which map was entered; state rows "event starts" / "event ends"
    set_by                     the event script (flowchart/entry point) that most likely made the change
    npc                        who speaks in the dialogue before the change in that script
    dialogue                   the --lines lines spoken nearest before the change in that script, as "Speaker: text"
                               joined by " || " (a script that branches can give lines from different branches;
                               a HavePlayedEvent row: the event's first lines)
    match                      how set_by was picked:
                                 arrival the entrance Link just used plays that script on arrival
                                 event   a HavePlayedEvent for that script changed within EVENT_WINDOW seconds
                                 only    it's the only script that sets this field (to this value)
                                 map     several do; this one is named after the map Link was on
                                 guess   several do and nothing told them apart: the first is shown
                                 played  the row is a HavePlayedEvent flag; set_by is that event
                                 played-script   the same, but only its script is known (the event's name
                                         starts with the script's): the dialogue is the script's first lines
                                 none    no event script sets it: the game's code did (or a script by a way not
                                         read here, e.g. a logic graph)
                                 sloperinaGZ   changed by the mod's menu or cheats
    other_scripts              how many other scripts also set it

The scripts tell who can make a change, not who did: with several candidates the match is a best guess.

Needs the game dump (OOTMENU_NUP, see nupfs.py), zstandard and evfl (see requirements.txt).
"""
import argparse
import collections
import csv
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamefiles  # noqa: E402

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
FIELDS_JSON = os.path.join(DATA, "gamedata_fields.json")
STORY_JSON = os.path.join(DATA, "story.json")  # the quests: Step_ Enum names and stages the field list lacks
FLOW_DIR = "romfs/Event/EventFlow/"
MALS = "romfs/Mals/{}.Product.110.sarc.zs"
EVENT_WINDOW = 15.0  # seconds: a HavePlayedEvent change this near a row ties the row to that event
PLAYED_PREFIX = "HavePlayedEvent."
TALK_ACTIONS = ("EventTalk",)
EVENT_STARTER = "Npc_EventStarter"  # the actor that started the event: the NPC Link talked to
AREA_WINDOW = 8.0  # seconds after entering a map in which a change is put down to entering it
ITEM_PREFIX = "IsGet."
VALUE_TYPES = ("Bool", "Int", "Enum", "Float", "String")
ADDED_COLUMNS = ["name", "group", "description", "cause", "set_by", "npc", "dialogue", "match", "other_scripts"]


def murmur3(text):
    """MurmurHash3_x86_32, seed 0: the game's field and Enum choice hashes (src/ootgz/gamedata.c)."""
    data = text.encode()
    c1, c2, h = 0xCC9E2D51, 0x1B873593, 0
    mask = 0xFFFFFFFF

    def rotl(x, r):
        return ((x << r) | (x >> (32 - r))) & mask

    whole = len(data) // 4 * 4
    for i in range(0, whole, 4):
        k = rotl((struct.unpack_from("<I", data, i)[0] * c1) & mask, 15) * c2 & mask
        h = (rotl(h ^ k, 13) * 5 + 0xE6546B64) & mask
    tail = 0
    for i, b in enumerate(data[whole:]):
        tail |= b << (8 * i)
    if len(data) & 3:
        h ^= rotl(tail * c1 & mask, 15) * c2 & mask
    h ^= len(data)
    h = (h ^ (h >> 16)) * 0x85EBCA6B & mask
    h = (h ^ (h >> 13)) * 0xC2B2AE35 & mask
    return h ^ (h >> 16)


# --- the fields ----------------------------------------------------------------------------------------------

def load_fields():
    """{hash: field record} from data/gamedata_fields.json, each Enum with "choices": {hash: name}."""
    fields = {}
    with open(FIELDS_JSON, encoding="utf-8") as f:
        for record in json.load(f):
            if record.get("t") == "Enum" and record.get("v"):
                record["choices"] = {murmur3(choice): choice for choice in record["v"]}
            fields[int(record["h"], 16)] = record
    with open(STORY_JSON, encoding="utf-8") as f:
        for key, quest in json.load(f)["quests"].items():
            record = fields.setdefault(int(key, 16), {"h": key[2:], "t": "Enum"})
            if not record.get("n"):
                record["n"], record["g"] = quest["name"], record.get("g") or "Quest steps"
            choices = record.setdefault("choices", {})
            for stage in quest.get("stages", []):
                choices.setdefault(murmur3(stage), stage)
    return fields


# --- messages (MSBT, UTF-8) ----------------------------------------------------------------------------------

def msbt_messages(data):
    """{label: raw text bytes} of an MSBT file."""
    if data[:8] != b"MsgStdBn":
        return {}
    sections, offset = {}, 0x20
    for _ in range(struct.unpack_from("<H", data, 0x0E)[0]):
        magic, size = data[offset:offset + 4], struct.unpack_from("<I", data, offset + 4)[0]
        sections[magic] = data[offset + 0x10:offset + 0x10 + size]
        offset = (offset + 0x10 + size + 15) & ~15
    labels_section, text_section = sections.get(b"LBL1"), sections.get(b"TXT2")
    if not labels_section or not text_section:
        return {}
    labels = {}
    for group in range(struct.unpack_from("<I", labels_section, 0)[0]):
        count, at = struct.unpack_from("<II", labels_section, 4 + 8 * group)
        for _ in range(count):
            length = labels_section[at]
            name = labels_section[at + 1:at + 1 + length].decode()
            labels[struct.unpack_from("<I", labels_section, at + 1 + length)[0]] = name
            at += 1 + length + 4
    n = struct.unpack_from("<I", text_section, 0)[0]
    starts = [struct.unpack_from("<I", text_section, 4 + 4 * i)[0] for i in range(n)] + [len(text_section)]
    return {labels[i]: text_section[starts[i]:starts[i + 1]] for i in range(n) if i in labels}


def msbt_text(raw):
    """Readable text: page breaks as " / ", game values as {Name}, the other tags (style, emotion) left out."""
    out, run, i = [], bytearray(), 0

    def end_run():
        out.append(run.decode("utf-8", "replace"))
        run.clear()

    while i < len(raw):
        c = raw[i]
        if c == 0x0E and i + 7 <= len(raw):
            group, kind, size = struct.unpack_from("<HHH", raw, i + 1)
            params = raw[i + 7:i + 7 + size]
            end_run()
            if group == 0 and kind == 4:
                out.append(" / ")
            elif group == 2 and len(params) >= 2:
                length = struct.unpack_from("<H", params, 0)[0]
                out.append("{" + params[2:2 + length].decode("utf-8", "replace") + "}")
            i += 7 + size
        elif c == 0x0F and i + 5 <= len(raw):
            i += 5
        elif c == 0:
            i += 1
        else:
            run.append(c)
            i += 1
    end_run()
    return " ".join("".join(out).split())


class Messages:
    def __init__(self, lang):
        self.files = gamefiles.sarc(gamefiles.read(MALS.format(lang)))
        self.parsed = {}

    def text(self, message_id):
        """The text of "EventFlowMsg/File:Label", or "" if it isn't there."""
        file_name, _, label = (message_id or "").partition(":")
        if file_name not in self.parsed:
            data = self.files.get(file_name + ".msbt")
            self.parsed[file_name] = msbt_messages(data) if data else {}
        raw = self.parsed[file_name].get(label)
        return msbt_text(raw) if raw else ""


# --- event scripts -------------------------------------------------------------------------------------------

def _ref(index):
    return getattr(index, "v", None) if index is not None else None


def successors(event):
    data = event.data
    found = []
    for attribute in ("nxt", "join"):
        if hasattr(data, attribute):
            found.append(_ref(getattr(data, attribute)))
    for target in getattr(data, "cases", {}).values():
        found.append(_ref(target))
    for target in getattr(data, "forks", []):
        found.append(_ref(target))
    return [e for e in found if e is not None]


def action_of(event):
    action = _ref(getattr(event.data, "actor_action", None))
    return getattr(action, "v", "") if action is not None else ""


def actor_of(event):
    actor = _ref(getattr(event.data, "actor", None))
    if actor is None:
        return ""
    identifier = actor.identifier
    return identifier.name if hasattr(identifier, "name") else str(identifier)


def params_of(event):
    params = getattr(event.data, "params", None)
    return dict(params.data) if params is not None else {}


def is_talk(event):
    return action_of(event) in TALK_ACTIONS and "MessageId" in params_of(event)


# a quest's stage is moved by a challenge step: ChallengeName X, StepName Y sets Step_X to Y (story.json's quests)
CHALLENGE_ACTIONS = ("EventExecuteProgressChallengeStep", "EventTriggerProgressChallengeStep",
                     "EventExecuteRollbackChallengeStep")


def set_target(event):
    """(field name, value written or None) if the event changes a field, else None."""
    action, params = action_of(event), params_of(event)
    if "GameData" in action and "Query" not in action and "GameDataName" in params:
        return params["GameDataName"], params.get("Value")
    if action in CHALLENGE_ACTIONS and params.get("ChallengeName") and params.get("StepName"):
        return "Step_" + params["ChallengeName"], params["StepName"]
    return None


class Setter:
    """One event in one script that changes a field."""

    def __init__(self, flow, entries, action, value, talks):
        self.flow, self.entries, self.action, self.value, self.talks = flow, entries, action, value, talks

    def name(self):
        return self.flow + ("/" + "+".join(sorted(self.entries)[:2]) if self.entries else "")


class Scripts:
    """Every event script: who sets each field, and how each event (entry point) opens."""

    def __init__(self, lines):
        self.setters = collections.defaultdict(list)  # field name -> [Setter]
        self.events = collections.defaultdict(list)  # lower-case event name -> [(flow, entry or None)]
        self.openings = {}  # (flow, entry or None) -> first talks [(speaker, message id)]
        self.lines = lines
        self.flow_names = None  # longest first, made when played() first needs it
        for path, _ in gamefiles.nup().walk():
            if path.startswith(FLOW_DIR) and path.endswith(".bfevfl.zs"):
                self._read(path)

    def _read(self, path):
        import evfl
        flow = evfl.EventFlow()
        try:
            flow.read(gamefiles.read(path))
        except Exception as error:  # a few files aren't flows evfl reads
            print("skipped {}: {}".format(path, error), file=sys.stderr)
            return
        chart = flow.flowchart
        if chart is None:
            return
        before = collections.defaultdict(list)
        for event in chart.events:
            for after in successors(event):
                before[id(after)].append(event)
        reached_by = collections.defaultdict(set)  # id(event) -> entry names
        self.events[chart.name.lower()].append((chart.name, None))
        first_talks = []
        for entry in chart.entry_points:
            self.events[entry.name.lower()].append((chart.name, entry.name))
            self.events["{}_{}".format(chart.name, entry.name).lower()].append((chart.name, entry.name))
            order = self._walk(_ref(entry.main_event), successors, lambda e: reached_by[id(e)].add(entry.name))
            talks = [(actor_of(e), params_of(e)["MessageId"]) for e in order if is_talk(e)][:self.lines]
            self.openings[(chart.name, entry.name)] = talks
            first_talks = first_talks or talks
        self.openings[(chart.name, None)] = first_talks
        for event in chart.events:
            target = set_target(event)
            if not target:
                continue
            order = self._walk(event, lambda e: before[id(e)])[1:]
            talks = [(actor_of(e), params_of(e)["MessageId"]) for e in order if is_talk(e)][:self.lines]
            self.setters[target[0]].append(
                Setter(chart.name, reached_by[id(event)], action_of(event), target[1], talks[::-1]))

    @staticmethod
    def _walk(start, step, visit=None):
        """Events from `start` in breadth-first order (nearest first) along `step`."""
        if start is None:
            return []
        order, seen, queue = [], {id(start)}, collections.deque([start])
        while queue:
            event = queue.popleft()
            order.append(event)
            if visit:
                visit(event)
            for nxt in step(event):
                if id(nxt) not in seen:
                    seen.add(id(nxt))
                    queue.append(nxt)
        return order

    def played(self, field_name):
        """(flow, entry) pairs a HavePlayedEvent.<event> field names: a script or entry point of that name, else
        the longest script name it starts with ("Npc_Kokiri_Mido_Adult_C1_N": Npc_Kokiri_Mido_Adult)."""
        event = field_name[len(PLAYED_PREFIX):].lower()
        if event in self.events:
            return self.events[event]
        if self.flow_names is None:
            self.flow_names = sorted({flow for pairs in self.events.values() for flow, _ in pairs}, key=len,
                                     reverse=True)
        for flow in self.flow_names:
            if event.startswith(flow.lower() + "_"):
                return [(flow, None)]
        return []


# --- the log -------------------------------------------------------------------------------------------------

def speaker(actor, flow):
    return flow if actor == EVENT_STARTER else actor


def dialogue_text(talks, flow, messages):
    return " || ".join("{}: {}".format(speaker(a, flow), messages.text(m) or m) for a, m in talks)


def npc_of(talks, flow):
    names = []
    for actor, _ in talks:
        name = speaker(actor, flow)
        if name and name not in names:
            names.append(name)
    return " + ".join(names)


def value_matches(setter, row):
    """The setter writes the row's new value (or we can't tell)."""
    new = row["new"]
    if setter.action in CHALLENGE_ACTIONS:
        return new == setter.value
    if setter.value is None or "Set" not in setter.action or "Copy" in setter.action:
        return True
    if isinstance(setter.value, bool):
        return new == ("1" if setter.value else "0")
    if isinstance(setter.value, int) and row["type"] == "Int":
        return new == str(setter.value)
    return True


def time_of(row):
    """Seconds into the file: the row's time plus the sessions before it (see session_times)."""
    return row.get("_t", 0.0)


def session_times(rows):
    """Give each row a time that keeps counting across sessions. The module adds every session to the same file and
    starts its time column again at 0 at each "start" row, so later sessions are moved past the earlier ones (with
    a gap wider than any window used here) to keep one session's rows from matching another's."""
    offset = last = 0.0
    session = 0
    for row in rows:
        try:
            t = float(row["time"])
        except (KeyError, ValueError):
            t = 0.0
        if row.get("type") == "start" and last:
            offset += last + 3600.0
            last = 0.0
            session += 1
        row["_t"], row["_session"] = offset + t, session
        last = max(last, t)


def pick(setters, row, recent_flows, arrival_flows):
    """(setter, match) for a row."""
    candidates = [s for s in setters if value_matches(s, row)] or setters
    flows = list(dict.fromkeys(s.flow for s in candidates))

    def of(flow):  # a script can set a field in several places: the first with dialogue before it
        return max((s for s in candidates if s.flow == flow), key=lambda s: bool(s.talks))

    for flow in flows:
        if flow in arrival_flows:
            return of(flow), "arrival"
    for flow in flows:
        if flow in recent_flows:
            return of(flow), "event"
    if len(flows) == 1:
        return of(flows[0]), "only"
    place = row.get("map", "").split("_")[0].lower()
    if place:
        for flow in flows:
            if place in flow.lower():
                return of(flow), "map"
    return of(flows[0]), "guess"


class Area:
    """An "area" row: Link entered `new` from `old`, by `entrance`, with the arrival event `event`."""

    def __init__(self, row):
        self.time, self.old, self.new = time_of(row), row.get("old", ""), row.get("new", "")
        parts = dict(part.strip().split(" ", 1) for part in row.get("note", "").replace("arrival event", "event")
                     .split(";") if " " in part.strip())
        self.entrance = parts.get("entrance", "-")
        self.event = parts.get("event", "-")

    def text(self):
        where = "entering {} from {}".format(self.new or "?", self.old or "?")
        if self.entrance not in ("", "-"):
            where += " (entrance {})".format(self.entrance)
        return where


def name_rows(rows, fields, scripts, messages):
    # context from the whole log: HavePlayedEvent changes (their scripts), map changes, item gets by frame
    played_at, areas, items_at = [], [], collections.defaultdict(list)
    session_times(rows)
    for row in rows:
        if row.get("type") == "area":
            areas.append(Area(row))
            continue
        field = fields.get(int(row["hash"], 16)) if row.get("hash") else None
        name = field.get("n", "") if field else ""
        if name.startswith(PLAYED_PREFIX) and row.get("new") not in ("", "0"):
            played_at.append((time_of(row), {flow for flow, _ in scripts.played(name)}))
        if name.startswith(ITEM_PREFIX) and row.get("new") == "1":
            items_at[(row["_session"], row.get("frame"))].append(name[len(ITEM_PREFIX):])
    for row in rows:
        for column in ADDED_COLUMNS:
            row[column] = ""
        kind = row.get("type")
        t = time_of(row)
        area = next((a for a in reversed(areas) if a.time <= t and t - a.time <= AREA_WINDOW), None)
        if kind == "area":
            row["name"] = "Sequence_CurrentBanc"
            row["cause"] = Area(row).text()
            continue
        if kind == "state":
            row["cause"] = "event starts" if row.get("new", "").startswith("Evt") else "event ends"
            continue
        if kind not in VALUE_TYPES or not row.get("hash"):
            continue
        field = fields.get(int(row["hash"], 16))
        if not field:
            row["cause"] = cause_without_script(row, area, items_at)
            continue
        name = field.get("n", "")
        row["name"], row["group"], row["description"] = name, field.get("g", ""), field.get("e", "")
        if field.get("choices"):
            for column in ("old", "new"):
                try:
                    row[column] = field["choices"].get(int(row[column], 16), row[column])
                except ValueError:
                    pass
        if row.get("source") == "sloperinaGZ":
            row["match"], row["cause"] = "sloperinaGZ", "sloperinaGZ (its menu or a cheat)"
            continue
        if kind == "String":
            continue
        setters = scripts.setters.get(name, []) if name else []
        arrival_flows = {flow for flow, _ in scripts.events.get(area.event.lower(), [])} if area else set()
        if name.startswith(PLAYED_PREFIX) and not setters:  # (21 scripts set a HavePlayedEvent themselves)
            events = scripts.played(name)
            if events:
                flow, entry = events[0]
                talks = scripts.openings.get((flow, entry), [])
                row["set_by"] = flow + ("/" + entry if entry else "")
                row["npc"], row["dialogue"] = npc_of(talks, flow), dialogue_text(talks, flow, messages)
                exact = entry is not None or flow.lower() == name[len(PLAYED_PREFIX):].lower()
                row["match"] = "played" if exact else "played-script"
                row["other_scripts"] = str(len(events) - 1)
                row["cause"] = cause_of_script(row, flow, area, flow in arrival_flows)
            else:
                row["match"] = "played"
                row["cause"] = cause_without_script(row, area, items_at)
            continue
        if not setters:
            row["match"] = "none"
            row["cause"] = cause_without_script(row, area, items_at)
            continue
        recent = set().union(*[flows for when, flows in played_at if abs(when - t) <= EVENT_WINDOW] or [set()])
        setter, match = pick(setters, row, recent, arrival_flows)
        row["set_by"], row["match"] = setter.name(), match
        row["npc"] = npc_of(setter.talks, setter.flow)
        row["dialogue"] = dialogue_text(setter.talks, setter.flow, messages)
        row["other_scripts"] = str(len({s.flow for s in setters}) - 1)
        if match in ("map", "guess") and area and not in_event(row):
            row["cause"] = area.text() + " (or the script {}, a guess)".format(setter.flow)
        else:
            row["cause"] = cause_of_script(row, setter.flow, area, match == "arrival", row["npc"])
    return rows


def in_event(row):
    return row.get("link_state", "").startswith("Evt")


def cause_of_script(row, flow, area, arrival, npc=""):
    if arrival and area:
        return "arrival event {} on {}".format(area.event, area.text())
    if row.get("link_state") == "EvtTalk":
        return "talking: {}".format(npc or flow)
    if in_event(row):
        return "event: {}".format(flow)
    return "script {}".format(flow)


def cause_without_script(row, area, items_at):
    name = row.get("name", "")
    items = items_at.get((row["_session"], row.get("frame")), [])
    if name.startswith(ITEM_PREFIX) and row.get("new") == "1":
        items = [name[len(ITEM_PREFIX):]]
    if items:
        return "getting {}".format(", ".join(items))
    if area:
        return area.text()
    if in_event(row):
        return "during an event ({}), not by a script action".format(row.get("link_state"))
    return "the game's code (no event script sets it)"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("logs", nargs="+", help="flags-NNN.csv files from sd:/sloperinaGZ/flaglog/")
    parser.add_argument("--lang", default="EUen", help="message language (romfs/Mals/<lang>...; default EUen)")
    parser.add_argument("--lines", type=int, default=3, help="dialogue lines before each change (default 3)")
    args = parser.parse_args()
    fields = load_fields()
    print("reading the event scripts...", file=sys.stderr)
    scripts = Scripts(args.lines)
    messages = Messages(args.lang)
    for path in args.logs:
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            columns = list(reader.fieldnames or [])
            rows = list(reader)
            # StoryEvents.csv has the module's own cause: keep it, and put this one beside it
            recorded = [row.get("cause", "") for row in rows] if "cause" in columns else None
            rows = name_rows(rows, fields, scripts, messages)
            if recorded is not None:
                for row, cause in zip(rows, recorded):
                    row["cause_from_scripts"], row["cause"] = row["cause"], cause
                columns.append("cause_from_scripts")
        out = os.path.splitext(path)[0] + "-named.csv"
        with open(out, "w", newline="", encoding="utf-8-sig") as f:  # the BOM: Excel reads it as UTF-8
            writer = csv.DictWriter(f, columns + [c for c in ADDED_COLUMNS if c not in columns], extrasaction="ignore")  # (name: recorded)
            writer.writeheader()
            writer.writerows(rows)
        counts = collections.Counter(row["match"] for row in rows if row["match"])
        print("{}: {} rows ({})".format(out, len(rows), ", ".join("{} {}".format(n, m) for m, n in counts.most_common())))


if __name__ == "__main__":
    main()
