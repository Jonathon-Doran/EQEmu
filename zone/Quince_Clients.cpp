#include "Quince.h"
#include "Quince_Clients.h"
#include "Quince_Events.h"

#include "embparser.h"

using namespace std;
extern QuestParserCollection *parse;

//	Log(Logs::General, Logs::Quests, "[QUINCE] client state for char %d loaded",
//	QuinceLog("client state for char %d loaded", character_id);

// ==============================================================
// Client_State is an idea for maintaining quest state separately
// from topology.
//
// A client may have more than one concurrent quest, which must be
// inferred from the nodes
//
// ==============================================================

Client_State::Client_State()
{
}

Client_State::~Client_State()
{
}

// ==============================================================
// Split load off to make it easier to store in containers
// ==============================================================
void Client_State::load_state(Client *c, int zone)
{
	stringstream ss;
	this -> client = c;
	this -> character_id = client -> CharacterID();

	QuinceLog("loading client state for character %d", character_id);

	string query = StringFormat(
		"SELECT trigger_id, node_id, sequence, state, satisfied "
		"FROM `quince_active_triggers` WHERE character_id=%d", character_id);

	set<int> reload_quests;
	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		QuinceLog("Error loading state for character: %s", results.ErrorMessage());
		return;
	}

	// Not finished.   b1, b0 are bit values
	
	for (auto row = results.begin(); row != results.end(); ++row)
	{
		int trigger_id = atoi(row[0]);
		int node_id = atoi(row[1]);
		int seq = atoi(row[2]);
		int state = atoi(row[3]);
		bool sat = (*row[4] == 1);

		ss << "client state load trigger %d: ";
		
		if (sat)
		{
			ss << " satisfied";
		}
		else
		{
			ss << " NOT satisfied";
		}

		QuinceLog(ss.str().c_str(), trigger_id);
		ss.str("");

		node_seq[node_id] = seq;
		trigger_state[trigger_id] = state;
		active_triggers.insert(pair<int,int> (node_id, trigger_id));
		satisfied[trigger_id] = sat;

		int quest_id = 0;
		// Insert code to lookup quest-id from cache
	
		// Insert code to load the node into the cache
		
		// Insert code to load the trigger into the trigger cache
	
		if (! is_satisfied(trigger_id))
		{
			required_triggers.insert(pair<int,int> (node_id, trigger_id));
			QuinceLog("loading required trigger %d", trigger_id);
		}
		else
		{
			QuinceLog("trigger %d is satisfied on load", trigger_id);
		}

		// insert code to push polled triggers
		
		reload_quests.insert(quest_id);
	}

	// insert code to reload specified quests
	
	QuinceLog("client state for char %d loaded", character_id);
}

// ==============================================================
// partial load of trigger state -- mainly for roll back
// ==============================================================
void Client_State::load_triggers_at_seq(int node_id, int seq)
{
}

// ==============================================================
// allow nodes to be rolled back
//
// consider waypoint triggers, which would require re-initialization before restart
// (waypoints do not save their state, must restart quest step if rezone)
// ==============================================================
void Client_State::set_rollback(int node_id, int offset)
{
}

void Client_State::clear_rollback(int node_id)
{
}

void Client_State::init_waypoints(int npc_eid)
{
}

bool Client_State::get_waypoint_state(int npc_eid, NPC_Waypoint_State& state)
{
	// FIXME
	return false;
}

void Client_State::remove_waypoint_state(int npc_eid)
{
}

// ==============================================================
// Access trigger state
// ==============================================================

int Client_State::get_trigger_state(int trigger_id)
{
	// FIXME
	return -1;
}

void Client_State::set_trigger_state(int trigger_id, int new_state)
{
}

// ==============================================================
// The "satisfied" flag is client-specific state, set when a trigger
// has satisfied all of its requirements and will permit the node to
// advance.
// ==============================================================
bool Client_State::is_satisfied(int trigger_id)
{
	// FIXME
	return false;
}

void Client_State::set_satisfied(int trigger_id)
{
}

// ==============================================================
// Unload client state information, used when a client zones out
// ==============================================================
void Client_State::unload()
{
}

// ==============================================================
// Update the database with current client state
// ==============================================================
void Client_State::save_state()
{
}

void Client_State::save_active_triggers()
{
}

// ==============================================================
// Save state for a particular trigger
// ==============================================================
void Client_State::save_trigger(int node_id, int trigger_id)
{
}

// ==============================================================
// Determine the zone where a trigger may fire.  This must be loaded from
// the database, since the trigger may not be cached yet.
// ==============================================================
int Client_State::zone_from_db(int trigger_id)
{
	// FIXME
	return -1;
}

// ==============================================================
// indicates whether the client is working on a particular node
// ==============================================================
bool Client_State::on_node(int node_id)
{
	Node_ID_Map::iterator iter;

	for (iter = node_seq.begin(); iter != node_seq.end(); ++iter)
	{
		QuinceLog("node %d => current sequence %d", iter -> first, iter -> second);
	}

	if (node_seq.find(node_id) != node_seq.end())
	{
		return true;
	}
	else
	{
		return false;
	}
}

// ==============================================================
// has_quest -- check if this quest is active for the client
// ==============================================================
bool Client_State::has_quest(int quest_id)
{
	ID_List::iterator iter;
	iter = find(active_quests.begin(), active_quests.end(), quest_id);

	if (iter == active_quests.end())
	{
		return false;
	}
	else
	{
		return true;
	}
}

bool Client_State::is_journaled(int trigger_id)
{
	// FIXME
	return false;
}

bool Client_State::start_quest(int root_node)
{
	if (on_node(root_node))
	{
		QuinceLog("start_quest: already working on node %d", root_node);
		return false;
	}

	QuinceLog("attempt to load node %d", root_node);

	QNodeCache::Instance().load_node(root_node);
	node_seq[root_node] = 0;

	// look up quest-id
	int quest_id;
	if (! QNodeCache::Instance().quest_for(root_node, quest_id))
	{
		QuinceLog("no quest for root node %d", root_node);
		return false;
	}

	// do not want subquests to start the main quest multiple times
	// (subquests start too, and share the same quest_id)
	
	if (! has_quest(quest_id))
	{
		QuinceLog("new quest!  add quest %d to active and send to journal",
			quest_id);
		active_quests.push_back(quest_id);

		// FIXME:  need to implement
		// send_task(quest_id);
	}

	// initally starting at the activation node
	// FIXME:  there are often two nodes at sequence 0
	// 			only one can cause the quest to be accepted invoke start_quest

	// FIXME:  need to implement
	advance_seq(root_node, 0);
	return true;
}

void Client_State::advance_seq(int node_id, int trigger_id)
{
	int character_id = client -> CharacterID();
	bool activity_complete = true;
	bool quest_complete = false;
	
	QuinceLog("advance_seq for node %d, trigger %d", node_id, trigger_id);

	// some sanity checks against internal errors
	if (! on_node(node_id))
	{
		QuinceLog("advance_seq:  internal error, not on node %d", node_id);
		return;
	}

	// from the node_id, look up the quest_id and sequence numbers
	int quest_id = -1;

	if (! QNodeCache::Instance().quest_for(node_id, quest_id))
	{
		QuinceLog("advance_seq: no quest for node %d", node_id);
		return;
	}

	int seq = node_seq[node_id];

	// FIXME:  implement
	// remove_active_triggers(node_id);

	// advance to the next sequence number and attempt to load new triggers
	// if no triggers are available, then the node is complete

	ID_List null_triggers;		// would otherwise complete during activation
	ID_List new_triggers = QTriggerCache::Instance().load_triggers_at_seq(node_id, seq+1,
		 character_id);

	if (new_triggers.size() > 0)
	{
		node_seq[node_id]++;
		QuinceLog("loaded %d triggers for node %d, seq %d into cache",
			new_triggers.size(), node_id, seq+1);

		QuestEventID type;
		ID_List::iterator iter;

		for (iter = new_triggers.begin(); iter != new_triggers.end();  ++iter)
		{
			int trigger_id = *iter;

			if (! QTriggerCache::Instance().getType(trigger_id, type))
			{
				QuinceLog("advance_seq:  unable to get type for trigger %d", trigger_id);
				continue;
			}

			QuinceLog("start activation of trigger %d (%s)", trigger_id,
				event_string(type).c_str());

			if (type == EVENT_SUBQUEST)
			{
				int subquest;

				QuinceLog("starting subquest");
				// FIXME:  get sub node and start quest
			}

			active_triggers.insert(pair<int, int> (node_id, trigger_id));

			if (! QTriggerCache::Instance().init_sat_for_trigger(trigger_id))
			{
				QuinceLog("activate required node %d, trigger %d",
					node_id, trigger_id);
				required_triggers.insert(pair<int,int> (node_id, trigger_id));
			}
			else
			{
				QuinceLog("trigger %d is initially satisfied", trigger_id);
				satisfied[trigger_id] = true;
			}

			// initially, no progress on this trigger
			// some triggers require # events before completion
			trigger_state[trigger_id] = 0;

			// Look up the zone for this trigger (0 = any)
			int zoneID;
			if (! QTriggerCache::Instance().get_zone(trigger_id, zoneID))
			{
				QuinceLog("advance_seq:  no zone for trigger");
				continue;
			}

			// check if in specific zone, or if trigger fires in any zone
			bool in_zone = (((uint32) zoneID == zone -> GetZoneID()) || (zoneID == 0));

			// proximity triggers are polled rather than discrete events
			// we need to periodically check to see if the client is near some point
			// (typically this is 1 sec)

			if (type == EVENT_PROXIMITY)
			{
				if (in_zone)
				{
					QuinceLog("pushing polled trigger");
					polled_triggers.push_back(trigger_id);
				}
			}
			else if (type == EVENT_NULL)
			{
				// null events fire immediately
				// they are used to allow dialog to be split across mulitple nodes/triggers
		
//				PerlembParser *parse = QuinceTriggers::Instance().getParser();
				QuinceLog("firing null trigger");

				string subroutine;
				if (! QTriggerCache::Instance().get_subroutine(trigger_id, subroutine))
				{
					QuinceLog("advance seq:  no subroutine for trigger");
					continue;
				}

				// FIXME:  Back in 2013 I removed this EventCommon
				//
				// It is not clear why this was done.  The effect was to cause NPC
				// dialog to not occur (because scripts were not run).
				// Why was this removed?

				// events seem to be going directly to the questparsercollection

				PerlembParser *parse = QuinceTriggers::Instance().getParser();
			
				parse -> EventQuince(EVENT_NULL, 0, nullptr, client->CastToNPC(), nullptr, nullptr,
					client, 0, false, nullptr, quest_id, subroutine);

				null_triggers.push_back(trigger_id);
			}
		}
	}
	else
	{
		// no triggers loaded at this sequence

		// need to think about this some more
		// where do we go once a node is finished?
		//
		// ... fire the corresponding subquest trigger (unless we are at the root)

		QuinceLog("no more triggers, node %d must have completed", node_id);

		fired_triggers.clear();


#if 0
		// look up the parent trigger
		int parent_trigger;
		if (QNodeCache::Instance().get_parent_trigger(node_id, parent_trigger))
		{
			QuinceLog("node has parent trigger %d", parent_trigger);

			// FIXME
			// QNodeCache::Instance.unload_node(node_id);
			//
			// something about pending map (noted on 1/5/2014 and 2/18/2014)

			// schedule_activity_complete(quest_id, trigger_id);
			// complete_trigger(parent_trigger);
		}
		else
		{
			QuinceLog("root node completed?");

			// FIXME
		}

		// fire null triggers, saved earlier
		ID_List::iterator n_iter;
		for (n_iter : null_triggers)
		{
			complete_trigger(*n_iter);
		}
#endif

		// FIXME
		// consider sending state to client
	}
}

void Client_State::complete_trigger(int trigger_id)
{
}

bool Client_State::is_visible(int quest_id, int trigger_id)
{
	// FIXME
	return false;
}

// 12/23/2013:  I think this is obsolete
// Perl handers should not be enabling/disabling triggers directly
void Client_State::remove_active_trigger(int trigger_id)
{
}	

void Client_State::remove_active_triggers(int node_id)
{
}

bool Client_State::has_fired(int trigger_id)
{
	// FIXME
	return false;
}

// ==============================================================
// display -- Display the Client state
//
// A diagnostic routine to verify client-state behavior.
// ==============================================================
void Client_State::display() const
{
	stringstream ss;

	if (client == NULL)
	{
		QuinceLog("no client associated with this client state object");
		return;
	}

	ss << "character: " << character_id;
	client -> Message(0, ss.str().c_str());
	ss.str("");

	QuinceLog("triggers for this character: ");

	vector<int> trigger_list;
	Node_ID_Map::const_iterator node_iter = node_seq.begin();

	while (node_iter != node_seq.end())
	{
		ss << "node " << node_iter -> first << " at seq " << node_iter -> second;

		client -> Message(0, ss.str().c_str());
		ss.str("");

		++node_iter;
	}

	client -> Message(0, "Client State (active triggers):");
	QuinceLog("active triggers: ");

	Trigger_ID_Set::const_iterator tis_iter = active_triggers.begin();

	while (tis_iter != active_triggers.end())
	{
		int node = tis_iter -> first;
		int trigger = tis_iter -> second;
		int seq = seq_for_node(node);

		ss << "node " << node << ": " << seq << " has active trigger " << trigger;
		client -> Message(0, ss.str().c_str());

		QuinceLog(ss.str().c_str());

		ss.str("");
		++tis_iter;
	}

	client -> Message(0, "required triggers: ");
	QuinceLog("required triggers: ");

	tis_iter = required_triggers.begin();
	while (tis_iter != required_triggers.end())
	{
		int node = tis_iter -> first;
		int trigger = tis_iter -> second;
		int seq = seq_for_node(node);

		ss << "node " << node << ": " << seq << " has required trigger " << trigger;
		client -> Message(0, ss.str().c_str());

		QuinceLog(ss.str().c_str());

		ss.str("");
		++tis_iter;
	}

	client -> Message(0, "active quests: ");
	QuinceLog("active quests: ");

	String_Map::const_iterator qt_iter = quest_title.begin();
	while (qt_iter != quest_title.end())
	{
		int qid = qt_iter -> first;
		string title = qt_iter -> second;

		int idx = -1;

		Integer_Map::const_iterator i_iter = quest_seq.find(qid);
		if (i_iter != quest_seq.end())
		{
			idx = i_iter -> second;
		}

		ss << "quest " << qid << ": " << title << " at index " << idx;

		client -> Message(0, ss.str().c_str());
		QuinceLog(ss.str().c_str());
		ss.str("");
		++qt_iter;
	}
}

// this is where slots are "assigned" to activities
// we can assume that task state has been sent to the client prior to
// any activities completing (otherwise the triggers wouldn't be active)
void Client_State::send_task_state(int quest_id)
{
}

// ==============================================================
// lookup the current sequence# for a quest node
//
// This sequence number marks the progress through the specified quest node.
// Sequences start at 0, and end when there are no more triggers.
// ==============================================================
int Client_State::seq_for_node(int node_id) const
{
	Node_ID_Map::const_iterator node_seq_iter = node_seq.find(node_id);

	if (node_seq_iter != node_seq.end())
	{
		return node_seq_iter -> second;
	}

	// not found
	return -1;
}

// mainly on zone-in, otherwise just single activity changes
void Client_State::send_task(int quest_id)
{
}

void Client_State::schedule_activity_complete(int quest_id, int trigger_id)
{
}

void Client_State::send_visible_activity(int slot, int trigger)
{
}

// sent once per quest
void Client_State::send_task_description(int quest_id, bool show_journal)
{
}

// act is the slot# for the step (client index)
void Client_State::send_activity_complete(int quest_id, int trigger_id, bool last)
{
}

void Client_State::cancel_task(int quest_id)
{
}

void Client_State::cancel_quest(int seq_id)
{
}

void Client_State::fail_quest(int quest_id)
{
}

void Client_State::process()
{
}

// ==============================================================
// process_global_event:  process QEvents which are not associated with a client
//
// Some events may occur outside of the context of a particular client
// (e.g. signals).  These are handled by passing the event to each Client_State
// object and allowing it to determine if a trigger should fire.
//
// Global events should be low frequency occurrences.
// ==============================================================

void Client_State::process_global_event(Event *event, int quest_id)
{
}
