#include "Quince.h"
#include "Quince_Clients.h"
#include "Quince_events.h"

#include "embparser.h"

using namespace std;

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
	client = c;
	character_id = client -> CharacterID();

	Log(Logs::General, Logs::Quests, "loading client state for character %d",
		character_id);

/*
	const char *Trigger_Query =
		"SELECT trigger_id, node_id, sequence, state, satisfied "
		"FROM `quince_active_triggers` WHERE character_id=%d AND "
		"(zone=%d OR zone=0)";
*/
	std::string query = StringFormat(
		"SELECT trigger_id, node_id, sequence, state, satisfied "
		"FROM `quince_active_triggers` WHERE character_id=%d", character_id);

	const char *error_msg = "[QUINCE] error loading state for character %d: %s";

	set<int> reload_quests;

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error loading state for character: %s",
			results.ErrorMessage());
		return;
	}

	for (auto row = results.begin(); row != results.end(); ++row)
	{
		int trigger_id = atoi(row[0]);
		int node_id = atoi(row[1]);
		int seq = atoi(row[2]);
		int state = atoi(row[3]);
		bool sat = (*row[4] == 1);

		Log(Logs::General, Logs::Quests, "state load trigger %d", trigger_id);
		if (sat)
			Log(Logs::General, Logs::Quests, "   satisfied");
		else
			Log(Logs::General, Logs::Quests, "   NOT satisfied");


		node_seq[node_id] = seq;
		trigger_state[trigger_id] = state;
		active_triggers.insert(pair<int,int> (node_id, trigger_id));
		satisfied[trigger_id] = sat;

		int quest_id = 0;
		if (! QNodeCache::Instance().quest_from_db(node_id, quest_id))
		{
			Log(Logs::General, Logs::Quests, "no quest for node %d", node_id);
			continue;
		}

		// one reference per trigger
		QNodeCache::Instance().load_node(node_id);
		QuinceTriggerCache::Instance().load_trigger(trigger_id, seq, character_id);

		if (! is_satisfied(trigger_id))
		{
			required_triggers.insert(pair<int,int> (node_id, trigger_id));
			Log(Logs::General, Logs::Quests, "loading req trigger %d", trigger_id);
		}
		else
		{
			Log(Logs::General, Logs::Quests, "trigger %d is satisfied on load",
				trigger_id);
		}

		QuestEventID type;
		if (QuinceTriggerCache::Instance().getType(trigger_id, type))
		{
			if (type == EVENT_PROXIMITY)
			{
				Log(Logs::Detail, Logs::Quests, "pushing polled trigger %d",
					trigger_id);
				polled_triggers.push_back(trigger_id);
			}
		}

		reload_quests.insert(quest_id);
	}

	// update appropriate quests
	set<int>::const_iterator iter = reload_quests.begin();
	while (iter != reload_quests.end())
	{
		int quest_id = *iter;
		
		send_task(quest_id);
		send_task_state(quest_id);
		active_quests.push_back(quest_id);
		++iter;
	}

	Log(Logs::General, Logs::Quests, "client state for char %d loaded", character_id);
}

// ==============================================================
// partial load of trigger state -- mainly for roll back
// ==============================================================
void Client_State::load_triggers_at_seq(int node_id, int seq)
{
	ID_List new_triggers = QuinceTriggerCache::Instance().load_triggers_at_seq(node_id, seq,
		character_id);
	ID_List::iterator iter;

	for (iter = new_triggers.begin(); iter != new_triggers.end(); ++iter)
	{
		int trigger_id = *iter;
		active_triggers.insert(pair<int,int> (node_id, trigger_id));
		trigger_state[trigger_id] = 0;
	}
}

// ==============================================================
// allow nodes to be rolled back
//
// consider waypoint triggers, which would require re-initialization before restart
// (waypoints do not save their state, must restart quest step if rezone)
// ==============================================================
void Client_State::set_rollback(int node_id, int offset)
{
	roll_back.insert(pair<int,int> (node_id, offset));
	Log(Logs::General, Logs::Quests, "set rollback for node %d to %d",
		node_id, offset);
}

void Client_State::clear_rollback(int node_id)
{
	roll_back.erase(node_id);
}

void Client_State::init_waypoints(int npc_eid)
{
	NPC *npc = entity_list.GetNPCByID(npc_eid);

	Log(Logs::General, Logs::Quests, "init_waypoints:  client state entity %d",
		npc_eid);

	if (npc == NULL)
	{
		Log(Logs::General, Logs::Quests, "init_waypoints:  no npc for entity %d",
			npc_eid);
		return;
	}

	NPC_Waypoint_State wp_state;
	// int grid_id = npc -> GetGrid();		even need grid id?

	wp_state.eid = npc_eid;
	wp_state.npc = npc;
	wp_state.position = 0;
	wp_state.client = client;
	wp_state.char_id = character_id;
	walking_npcs.push_back(wp_state);
}

bool Client_State::get_waypoint_state(int npc_eid, NPC_Waypoint_State& state)
{
	NPC_WPs::iterator iter;
	for (iter = walking_npcs.begin(); iter != walking_npcs.end(); ++iter)
	{
		if (iter -> eid == npc_eid)
		{
			state = *iter;
			return true;
		}
	}

	return false;
}

void Client_State::remove_waypoint_state(int npc_eid)
{
	NPC_WPs::iterator iter;
	for (iter = walking_npcs.begin(); iter != walking_npcs.end(); ++iter)
	{
		if (iter -> eid == npc_eid)
		{
			Log(Logs::General, Logs::Quests, "remove waypoint state for eid %d",
				npc_eid);
			walking_npcs.erase(iter);
			return;
		}
	}
}

// ==============================================================
// Access trigger state
// ==============================================================

int Client_State::get_trigger_state(int trigger_id)
{
	Trigger_ID_Map::const_iterator iter = trigger_state.find(trigger_id);

	// activation triggers do not have state, nor may they be completed in a traditional manner
	bool activation = QuinceTriggerCache::Instance().is_activation_trigger(trigger_id);

	if ((iter == trigger_state.end()) && ! activation)
	{
		Log(Logs::General, Logs::Quests, "get: no state for trigger %d", 
			trigger_id);
		return 1;		// trigger_id not found
	}
	else
	{
		Log(Logs::General, Logs::Quests, "getting trigger %d state %d",
			trigger_id, iter -> second);
		return iter->second;
	}
}

void Client_State::set_trigger_state(int trigger_id, int new_state)
{
	Trigger_ID_Map::const_iterator iter = trigger_state.find(trigger_id);

	bool activation = QuinceTriggerCache::Instance().is_activation_trigger(trigger_id);

	if ((iter == trigger_state.end()) && ! activation)
	{
		Log(Logs::General, Logs::Quests, "set: no state for trigger %d", 
			trigger_id);
		return;
	}

	Log(Logs::General, Logs::Quests, "trigger %d state set to %d",
		trigger_id, new_state);

	trigger_state[trigger_id] = new_state;
}

// ==============================================================
// The "satisfied" flag is client-specific state, set when a trigger
// has satisfied all of its requirements and will permit the node to
// advance.
// ==============================================================
bool Client_State::is_satisfied(int trigger_id)
{
	Bool_Map::const_iterator iter = satisfied.find(trigger_id);

	if (iter == satisfied.end())
	{
		return true;
	}

	return iter->second;
}

void Client_State::set_satisfied(int trigger_id)
{
	satisfied[trigger_id] = true;

	int node_id;
	if (! QuinceTriggerCache::Instance().node_for(trigger_id, node_id))
	{
		// already logged
		return;
	}

	// obtain a list of all required triggers for this node
	pair<Trigger_ID_Set::iterator, Trigger_ID_Set::iterator> trig_range =
		required_triggers.equal_range(node_id);

	Log(Logs::General, Logs::Quests, "satisfy trigger %d", trigger_id);

	Trigger_ID_Set::iterator iter;
	for (iter = trig_range.first; iter != trig_range.second; ++iter)
	{
		if (iter -> second == trigger_id)
		{
			required_triggers.erase(iter);
			return;
		}
	}

}

// ==============================================================
// Unload client state information, used when a client zones out
// ==============================================================
void Client_State::unload()
{
	save_state();

	// remove references to quest objects

	Trigger_ID_Set::const_iterator tis_iter = active_triggers.begin();
	while (tis_iter != active_triggers.end())
	{
		int node = tis_iter -> first;
		int trigger = tis_iter -> second;

		QNodeCache::Instance().unload_node(node);
		QuinceTriggerCache::Instance().unload_trigger(trigger, character_id);
		++tis_iter;
	}

	QModifications::Instance().unload_char(character_id);
	client = NULL;
	node_seq.erase(node_seq.begin(), node_seq.end());
	trigger_state.erase(trigger_state.begin(), trigger_state.end());
	active_triggers.erase(active_triggers.begin(), active_triggers.end());
	required_triggers.erase(required_triggers.begin(), required_triggers.end());
	roll_back.clear();
}

// ==============================================================
// Update the database with current client state
// ==============================================================
void Client_State::save_state()
{
	Log(Logs::General, Logs::Quests, "saving client state");

	// on a rollback:  clear active triggers and reload node from previous seq
	Integer_Map::const_iterator iter;
	for (iter = roll_back.begin(); iter != roll_back.end(); ++iter)
	{
		int node_id = iter -> first;
		int offset = iter -> second;
		int seq = node_seq[node_id] + offset;

		Log(Logs::General, Logs::Quests, "rolling back node %d to seq %d",
			node_id, seq);
		remove_active_triggers(node_id);		// will remove from database too
		node_seq[node_id] = seq;
		load_triggers_at_seq(node_id, seq);
	}

	save_active_triggers();
}

void Client_State::save_active_triggers()
{
	Log(Logs::General, Logs::Quests, "saving client trigger state");

	Trigger_ID_Set::const_iterator tis_iter = active_triggers.begin();

	while (tis_iter != active_triggers.end())
	{
		int node = tis_iter -> first;
		int trigger = tis_iter -> second;

		Log(Logs::General, Logs::Quests, "cs save %d:%d", node, trigger);
		save_trigger(node, trigger);
		++tis_iter;
	}
}

// ==============================================================
// Save state for a particular trigger
// ==============================================================
void Client_State::save_trigger(int node_id, int trigger_id)
{
	character_id = client -> CharacterID();
	int zone = zone_from_db(trigger_id);

	int quest_id;

	if (! QNodeCache::Instance().quest_for(node_id, quest_id))
	{
		Log(Logs::General, Logs::Quests, "save trigger:  no quest id for node %d",
			node_id);
		return;
	}

	int seq = seq_for_node(node_id);
	int state = trigger_state[trigger_id];
	char sat_value = satisfied[trigger_id]?'1':'0';

	std::string query = StringFormat(
		"REPLACE INTO `quince_active_triggers` SET trigger_id=%d, node_id=%d, "
		"sequence=%d, state=%d, character_id=%d, zone=%d, quest_id=%d, "
		"satisfied=b'%c'", trigger_id, node_id, seq, state, character_id, zone,
		quest_id, sat_value);

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error saving trigger: %s",
			results.ErrorMessage());
	}
}

// ==============================================================
// Determine the zone where a trigger may fire.  This must be loaded from
// the database, since the trigger may not be cached yet.
// ==============================================================
int Client_State::zone_from_db(int trigger_id)
{
	std::string query = StringFormat(
		"SELECT zone FROM `quince_triggers` WHERE id=%d", trigger_id);

	const char *error_msg = "[QUINCE] error loading zone-id";

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "trigger %d not found: %s",
			trigger_id);
		return -1;
	}

	auto row = results.begin();
	return atoi(row[0]);
}

// ==============================================================
// indicates whether the client is working on a particular node
// ==============================================================
bool Client_State::on_node(int node_id)
{
	Node_ID_Map::iterator iter;
	for (iter = node_seq.begin(); iter != node_seq.end(); ++iter)
	{
		Log(Logs::General, Logs::Quests, "%d => %d",
			iter -> first, iter -> second);
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

bool Client_State::has_quest(int quest_id)
{
	ID_List::iterator iter;
	iter = std::find(active_quests.begin(), active_quests.end(), quest_id);

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
	Integer_Map::iterator iter = journal_slots.find(trigger_id);
	if (iter == journal_slots.end())
	{
		return false;
	}
	else
	{
		return true;
	}
}

bool Client_State::start_quest(int root_node)
{
	if (on_node(root_node))
	{
		Log(Logs::General, Logs::Quests, "start_quest: already working on node %d",
			root_node);
		return false;
	}

	Log(Logs::General, Logs::Quests, "attempt to load node %d", root_node);

	QNodeCache::Instance().load_node(root_node);
	// 1/5/2014
	node_seq[root_node] = 0;

	int quest_id;
	if (! QNodeCache::Instance().quest_for(root_node, quest_id))
	{
		Log(Logs::General, Logs::Quests, "no quest for root node");
		return false;
	}

	// do not want subquests to start the main quest multiple times
	if (! has_quest(quest_id))
	{
		Log(Logs::General, Logs::Quests, "new quest, add quest %d to active and send journal",
			quest_id);
		active_quests.push_back(quest_id);
		send_task(quest_id);
	}

	// initially starting at the activation node
	advance_seq(root_node, 0);

	return true;
}

void Client_State::advance_seq(int node_id, int trigger_id)
{
	int quest_id;
	int character_id = client -> CharacterID();
	Log(Logs::General, Logs::Quests, "advance_seq for node %d, trigger %d", node_id,
			trigger_id);

	bool activity_complete = true;
	bool quest_complete = false;

	// some sanity checks against internal errors
	if (!on_node(node_id))
	{
		Log(Logs::General, Logs::Quests, "advance_seq: not currently on node %d",
			node_id);
		return;
	}

	// from the node_id, look up the quest_id and sequence number
	if (! QNodeCache::Instance().quest_for(node_id, quest_id))
	{
		Log(Logs::General, Logs::Quests, "no quest node %d", node_id);
		return;
	}

	int seq = node_seq[node_id];

	remove_active_triggers(node_id);

	// advance to the next sequence number and attempt to load new triggers
	// if no triggers are available then the node is complete

	ID_List null_triggers;   // would otherwise complete during activation
	ID_List new_triggers = QuinceTriggerCache::Instance().load_triggers_at_seq(node_id, seq+1, character_id);
	if (new_triggers.size() > 0)
	{
		node_seq[node_id]++;
		Log(Logs::General, Logs::Quests, "loaded %d triggers for node %d, seq %d", 
			new_triggers.size(), node_id, seq+1);

		QuestEventID type = EVENT_HP;		// something non-zero

		// activate these triggers
		ID_List::iterator iter;
		for (iter = new_triggers.begin(); iter != new_triggers.end(); ++iter)
		{
			int trigger_id = *iter;
			Log(Logs::General, Logs::Quests, "start activate of trig %d", trigger_id);

			if (! QuinceTriggerCache::Instance().getType(trigger_id, type))
			{
				Log(Logs::General, Logs::Quests, "unable to get type for trigger %d",
					trigger_id);
				continue;
			}

			if (type == EVENT_SUBQUEST)
			{
				int subquest;
				Log(Logs::General, Logs::Quests,  "starting subquest");
				if (QuinceTriggerCache::Instance().get_sub_node(trigger_id, subquest))
				{
					Log(Logs::General, Logs::Quests,  "subquest %d", subquest);
					start_quest(subquest);
				}
			}
			else
			{
				Log(Logs::General, Logs::Quests,  "activate %s", 
					event_string(type).c_str());
			}

#if 0
			if (type == EVENT_WAYPOINT_ARRIVE)
			{
				Log(Logs::General, Logs::Quests,  "schedule rollback for node %d, seq %d",
					node_id, node_seq[node_id] - 1);

				// rollback cleared when trigger completes
				set_rollback(node_id, node_seq[node_id] - 1);
			}
#endif
			active_triggers.insert(pair<int, int>(node_id, trigger_id));

			if (! QuinceTriggerCache::Instance().init_sat_for_trigger(trigger_id))
			{
				Log(Logs::General, Logs::Quests,  "activate req node %d, trig %d", 
					node_id, trigger_id);
				required_triggers.insert(pair<int,int>(node_id, trigger_id));
			}
			else
			{
				Log(Logs::General, Logs::Quests,  "trigger %d init sat", 
					trigger_id);
				satisfied[trigger_id] = true;
			}

			trigger_state[trigger_id] = 0;
			int zoneID;

			if (! QuinceTriggerCache::Instance().get_zone(trigger_id, zoneID))
			{
				Log(Logs::General, Logs::Quests,  "adv_seq:  no zone for trigger");
				continue;
			}

			bool in_zone = (((uint32) zoneID == zone->GetZoneID()) || (zoneID==0));
			if (type == EVENT_PROXIMITY)
			{
				Log(Logs::Detail, Logs::Quests,  "pushing polled trigger %d",
					trigger_id);
				if (in_zone)
				{
					polled_triggers.push_back(trigger_id);
				}
			}

			// null events fire immediately
			// they are used to allow dialog to be split across multiple
			// nodes/triggers
			if (type == EVENT_NULL)
			{
				int quest_id;
				if (! QuinceTriggerCache::Instance().quest_for(trigger_id, quest_id))
				{
					Log(Logs::General, Logs::Quests,  "adv_seq:  no quest for trigger");
						
					continue;
				}

				PerlembParser *parse = QuinceTriggers::Instance().getParser();
				Log(Logs::General, Logs::Quests,  "firing null trigger");
				string subroutine;
				if (! QuinceTriggerCache::Instance().get_subroutine(trigger_id, 
					subroutine))
				{
					Log(Logs::General, Logs::Quests, "adv_seq:  no subroutine for trigger");
						
					continue;
				}

/*
 * 	removed 2/13/2013:   what was the point of this?  was this to cause the null script to run?
 *
 *   9/7/2016, putting this back in.  NPC dialog was not being issued because this was missing.
 *   Whatever the problem was in 2013, it needs to be documented and fixed differently.
 */
				string package_name = QuinceTriggers::Instance().packageName(quest_id);

				parse -> EventQuince(EVENT_NULL, 0, nullptr, client->CastToNPC(), nullptr, nullptr, client, 0, false,
					nullptr, package_name, subroutine);


/*
				parse -> EventNPC(EVENT_NULL, client->CastToNPC(), NULL, 
					"", 0, quest_id, subroutine);
*/
				//complete_trigger(trigger_id);		// not visible activity
				null_triggers.push_back(trigger_id);
			}
		}

		Log(Logs::General, Logs::Quests, "AC: last new trigger processed -- act complete");

		schedule_activity_complete(quest_id, trigger_id);
	}
	else		// no new triggers at this seq
	{
		// need to think about this some more
		// where do we go once a node is finished?

		// ... fire the corresponding subquest trigger (unless we are at the root)
		Log(Logs::General, Logs::Quests, "node %d must have completed", 
			node_id);

		fired_triggers.clear();
		int parent_trigger;

		if (QNodeCache::Instance().get_parent_trigger(node_id, parent_trigger))
		{
			Log(Logs::General, Logs::Quests, "node has parent trigger %d", 
				parent_trigger);
			QNodeCache::Instance().unload_node(node_id);
			// 1/5/2014, 2/18/2014 pending map
			schedule_activity_complete(quest_id, trigger_id);
			complete_trigger(parent_trigger);
		}
		else
		{
			Log(Logs::General, Logs::Quests, "root node completed?");

			if (is_journaled(trigger_id))
			{
				if (pending_complete.find(quest_id) != pending_complete.end())
				{
					Log(Logs::General, Logs::Quests, 
						"pending %d when quest completing",
						pending_complete[quest_id]);
				}

				Log(Logs::General, Logs::Quests, "final quest complete sent");
				send_activity_complete(quest_id, trigger_id, quest_complete);
			}
			else
			{
				if (pending_complete[quest_id] != -1)
				{
					send_activity_complete(quest_id, pending_complete[quest_id],
						quest_complete);
				}
			}

			cancel_task(quest_id);		// 12/24/2013
			QNodeCache::Instance().unload_node(node_id);
		}

		node_seq.erase(node_id);
	}

	ID_List::iterator n_iter;
	for (n_iter = null_triggers.begin(); n_iter != null_triggers.end(); ++n_iter)
	{
		complete_trigger(*n_iter);
	}

	Log(Logs::General, Logs::Quests, 
			"advance seq for %d finishing, consider sending state",
			trigger_id);

	string task;
	// try restricting task updates to activities visible in journal
	if (QuinceTriggerCache::Instance().get_task(trigger_id, task))
	{
		if (task.compare("") != 0)
		{
			send_task_state(quest_id);
			Log(Logs::General, Logs::Quests, 
				"just sent task state after adv trigger %d", trigger_id);
		}
		else
		{
			Log(Logs::General, Logs::Quests, "Empty task string for trigger %d -- n o state sent", trigger_id);
		}
	}
	else
	{
		Log(Logs::General, Logs::Quests, "No task for trigger %d -- no state sent", trigger_id);
	}
}

void Client_State::complete_trigger(int trigger_id)
{
	static int fire_count = 0;
	int node_id;

	bool activity_complete = true;
	bool quest_complete = false;

	// FIXME: keep from looping forever
	if (fire_count++ >= 500)
	{
		return;
	}

	if (QuinceTriggerCache::Instance().is_activation_trigger(trigger_id))
	{
		Log(Logs::General, Logs::Quests, "skip complete activation trigger");
		return;
	}

	// repeatable just means that we don't remove trigger from list now
	if (QuinceTriggerCache::Instance().is_repeatable_trigger(trigger_id))
	{
		Log(Logs::General, Logs::Quests, "fire repeatable trigger %d", 
			trigger_id);
	}
	else
	{
		Log(Logs::General, Logs::Quests, "fire non-repeatable trigger %d", 
			trigger_id);
	}

	if (! QuinceTriggerCache::Instance().node_for(trigger_id, node_id))
	{
		Log(Logs::General, Logs::Quests, 
			"complete trigger: cannot find node for trigger %d",
			trigger_id);
		return;
	}

	if (!on_node(node_id))
	{
		Log(Logs::General, Logs::Quests, 
			"complete trigger: not currently on node %d", node_id);
		return;
	}

	int seq = node_seq[node_id];
	int quest_id;

	if (! QNodeCache::Instance().quest_for(node_id, quest_id))
	{
		Log(Logs::General, Logs::Quests, "complete trigger: no quest for %d",
			trigger_id);
		return;
	}

	// do we remove trigger?
	// proximity?  never
	// collect/kill -- when state reaches threshold
	// others?  yes
	QuestEventID trigger_type;
	if (! QuinceTriggerCache::Instance().getType(trigger_id, trigger_type))
	{
		Log(Logs::General, Logs::Quests, "CT: no quest type for %d",
			trigger_id);
		return;
	}

#if 0
	// 1/26/2014 completion should not cause removal.  That should occur when
	// seq advances and the trigger is removed from client state

	// remove from polled list (if this is a polled trigger)
	polled_triggers.remove(trigger_id);
#endif

	// if !repeatable then check count/state
	if (! QuinceTriggerCache::Instance().is_repeatable_trigger(trigger_id))
	{
		int state;
		int count;

		state = trigger_state[trigger_id];
		QuinceTriggerCache::Instance().get_count(trigger_id, count);

		Log(Logs::General, Logs::Quests, "trigger state at %d/%d",
					state, count);

		if (state < count)
		{
			Log(Logs::General, Logs::Quests, "trigger not complete, needs update");

			// trigger not complete (cannot break) but needs update
			send_task_state(quest_id);
			return;
		}

		Log(Logs::General, Logs::Quests, "trigger %d complete", trigger_id);
	}

	// either a repeatable trigger has completed
	// or a countable trigger has reached its limit

	set_satisfied(trigger_id);

	// see if any more required triggers exist
	Trigger_ID_Set::iterator iter = required_triggers.find(node_id);

	// if no more reqd, then complete and advance seq
	if (iter == required_triggers.end())
	{
		Log(Logs::General, Logs::Quests, 
			"node %d completed last trigger at seq %d", node_id, seq);

		// doing this early leave stuff in the journal?
		if (is_visible(quest_id, trigger_id))
		{
			// force redraw
			send_task_description(quest_id, true);
		}

		schedule_activity_complete(quest_id, trigger_id);

		Log(Logs::General, Logs::Quests, "about to advance trigger %d",
			trigger_id);
		advance_seq(node_id, trigger_id);

		Log(Logs::General, Logs::Quests, "%d journal slots",
				journal_slots.size());

		// 1/5/2014:  do we still get quest complete?
		send_task_state(quest_id);	// 12-23a
		Log(Logs::General, Logs::Quests, "after advance for last trigger");
	}
	else  // still more to work on
	{
		Log(Logs::General, Logs::Quests, 
			"node %d complete middle trigger %d", node_id, trigger_id);
		schedule_activity_complete(quest_id, trigger_id);
		send_task_state(quest_id);		// 12-23b
	}
}

bool Client_State::is_visible(int quest_id, int trigger_id)
{
	return journal_slots.find(trigger_id) != journal_slots.end();
}

// 12/23/2013:  I think this is obsolete
// Perl handers should not be enabling/disabling triggers directly
void Client_State::remove_active_trigger(int trigger_id)
{
	Trigger_ID_Map::iterator iter = trigger_state.find(trigger_id);

	int node_id;

	Log(Logs::General, Logs::Quests, 
			"attempt to remove active trigger %d", trigger_id);
	if (! QuinceTriggerCache::Instance().node_for(trigger_id, node_id))
	{
		Log(Logs::General, Logs::Quests, 
			"remove active trigger: cannot find node for trigger %d", trigger_id);
		return;
	}

	if (iter != trigger_state.end())
	{
		trigger_state.erase(iter);
	}

	// remove the trigger from the active list

	Log(Logs::General, Logs::Quests, "removing from the active list");

	std::pair<Trigger_ID_Set::iterator, Trigger_ID_Set::iterator> trig_range =
		active_triggers.equal_range(node_id);

	// have at least one trigger for this node?
	// cannot just use erase as we want to delete one element of multimap
	if (trig_range.first != active_triggers.end())
	{
		do
		{
			// is the trigger in the active list?
			if (trig_range.first -> second == trigger_id)
			{
				Log(Logs::General, Logs::Quests, "found on active list");
				active_triggers.erase(trig_range.first);
				break;
			}

		} while (++trig_range.first != trig_range.second);
	}

	trig_range = required_triggers.equal_range(node_id);
	if (trig_range.first != required_triggers.end())
	{
		do
		{
			// is the trigger in the active list?
			if (trig_range.first -> second == trigger_id)
			{
				Log(Logs::General, Logs::Quests, "found on required list");
				required_triggers.erase(trig_range.first);
				break;
			}

		} while (++trig_range.first != trig_range.second);
	}

	Log(Logs::General, Logs::Quests, "remove ref from cache");

	// remove reference from cache
	QuinceTriggerCache::Instance().unload_trigger(trigger_id, character_id);
	satisfied.erase(trigger_id);
}	

void Client_State::remove_active_triggers(int node_id)
{
	vector<int> ids_to_remove;

	// iterate over all active triggers for this node
	std::pair<Trigger_ID_Set::iterator, Trigger_ID_Set::iterator> trig_range =
		active_triggers.equal_range(node_id);

	Trigger_ID_Set::iterator iter;
	for (iter = trig_range.first; iter != trig_range.second; ++iter)
	{
		ids_to_remove.push_back(iter -> second);
	}

	// remove all triggers found
	active_triggers.erase(trig_range.first, trig_range.second);

	for (unsigned int i = 0; i < ids_to_remove.size(); i++)
	{
		int trigger_id = ids_to_remove[i];

		// remove cached entry
		QuinceTriggerCache::Instance().unload_trigger(trigger_id, character_id);

		QuinceTriggerCache::Instance().remove_active_from_database(trigger_id, 
				character_id);

		// remove from polled list (if this is a polled trigger)
		polled_triggers.remove(trigger_id);

		trigger_state.erase(trigger_id);
		satisfied.erase(trigger_id);
	}
}

bool Client_State::has_fired(int trigger_id)
{
	ID_List::const_iterator iter = 
		std::find(fired_triggers.begin(), fired_triggers.end(), trigger_id);
	return iter != fired_triggers.end();
}

void Client_State::display() const
{
	if (client == NULL)
	{
		Log(Logs::General, Logs::Quests, "no client associated with state");
		return;
	}

	stringstream ss;

	ss << "character: " << character_id;
	client -> Message(0, ss.str().c_str());
	ss.str("");

	Node_ID_Map::const_iterator node_iter = node_seq.begin();

	vector<int> trig_list;

	while (node_iter != node_seq.end())
	{
		ss << "node " << node_iter -> first << " at seq " << node_iter -> second;

		client -> Message(0, ss.str().c_str());
		ss.str("");

		++node_iter;
	}

	client -> Message(0, "Client State (active triggers):");
	Log(Logs::General, Logs::Quests, "active triggers:");

	Trigger_ID_Set::const_iterator tis_iter = active_triggers.begin();

	while (tis_iter != active_triggers.end())
	{
		int node = tis_iter -> first;
		int trigger = tis_iter -> second;
		int seq = seq_for_node(node);

		ss << "node " << node << ":" << seq << " has active trigger " << trigger;
		client -> Message(0, ss.str().c_str());

		Log(Logs::General, Logs::Quests, ss.str().c_str());
		ss.str("");

		++tis_iter;
	}

	client -> Message(0, "required triggers:");
#if LOG_CACHE
	Log(Logs::General, Logs::Quests, "required triggers:");
#endif

	tis_iter = required_triggers.begin();
	while (tis_iter != required_triggers.end())
	{
		int node = tis_iter -> first;
		int trigger = tis_iter -> second;
		int seq = seq_for_node(node);

		ss << "node " << node << ":" << seq << " has required trigger " << trigger;
		client -> Message(0, ss.str().c_str());
#if LOG_CACHE
		Log(Logs::General, Logs::Quests, ss.str().c_str());
#endif

		ss.str("");
		++tis_iter;
	}

	ss.str("");

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
		ss << "Quest Overview (quest " << qid << "):" << title << " at index " 
			<< idx;
		client -> Message(0, ss.str().c_str());
#if LOG_CACHE
		Log(Logs::General, Logs::Quests, ss.str().c_str());
#endif
		ss.str("");
		++qt_iter;
	}
}

// this is where slots are "assigned" to activities
// we can assume that task state has been sent to the client prior to
// any activities completing (otherwise the triggers wouldn't be active)
void Client_State::send_task_state(int quest_id)
{
	int slot = 0;		// must run from 0 to n, conseq
	journal_slots.clear();

	Log(Logs::General, Logs::Quests, "send_task_state %d", quest_id);
	Log(Logs::General, Logs::Quests, "%d active triggers", active_triggers.size());

	// send_task(quest_id);

	Trigger_ID_Set::const_iterator tis_iter = active_triggers.begin();
	while (tis_iter != active_triggers.end())
	{
		int trigger = tis_iter -> second;
		int node_id;
		int trig_quest;
		string task;

		Log(Logs::General, Logs::Quests, "   * consider trig %d", trigger);

		if (! QuinceTriggerCache::Instance().node_for(trigger, node_id))
		{
			Log(Logs::General, Logs::Quests, "no node for trigger %d",
				trigger);
			++tis_iter;
			continue;
		}

		if (! QNodeCache::Instance().quest_for(node_id, trig_quest))
		{
			Log(Logs::General, Logs::Quests, "no quest for node %d",
				node_id);
			++tis_iter;
			continue;
		}

		QuestEventID type;
		if (! QuinceTriggerCache::Instance().getType(trigger, type))
		{
			Log(Logs::General, Logs::Quests, "no type for trigger %d",
				trigger);
			++tis_iter;
			continue;
		}

#if 0
		// filter out internal triggers
		if ((type == EVENT_SUBQUEST) || (type == EVENT_PROXIMITY) ||
			(type == EVENT_SIGNAL))
		{
			++tis_iter;
			continue;
		}
#endif
		if (QuinceTriggerCache::Instance().get_task(trigger, task))
		{
			if ((task.compare("") == 0) || (task.compare("null") == 0))
			{
				Log(Logs::General, Logs::Quests,  "trigger %d no task",
					trigger);
				++tis_iter;
				continue;
			}
			else
			{
				Log(Logs::General, Logs::Quests,  "trigger %d task: '%s'",
					trigger, task.c_str());
			}
		}

		Log(Logs::General, Logs::Quests,  "trigger quest %d", trig_quest); 

		// active trigger for our quest
		if (trig_quest == quest_id)
		{
			journal_slots[trigger] = slot;
			Log(Logs::General, Logs::Quests,  "activity trigger %d slot %d",
				trigger, slot);
			
			send_visible_activity(slot, trigger);
			slot++;
		}
		++tis_iter;
	}
}

int Client_State::seq_for_node(int node_id) const
{
	Node_ID_Map::const_iterator node_seq_iter = node_seq.find(node_id);

	if (node_seq_iter != node_seq.end())
	{
		return node_seq_iter -> second;
	}

	return -1;
}

// mainly on zone-in, otherwise just single activity changes
void Client_State::send_task(int quest_id)
{
	Log(Logs::General, Logs::Quests, "******* Client_state send quest %d to client ******* ", quest_id);

	// populate with some test data
	string title;
	if (! Quince::Instance().get_quest_title(quest_id, title))
	{
		Log(Logs::General, Logs::Quests, "No quest title, abort send_task for quest %d", quest_id);
		return;
	}

	quest_title[quest_id] = title;
	quest_desc[quest_id] = string("Assist Farmer Jones");
	quest_seq[quest_id] = 0;			// client slot

	Log(Logs::General, Logs::Quests, "Task description sent for quest %d", quest_id);

	send_task_description(quest_id);
}

void Client_State::schedule_activity_complete(int quest_id, int trigger_id)
{
	Integer_Map::iterator js_iter = journal_slots.find(trigger_id);
	if (js_iter == journal_slots.end())
	{
		// not in the quest journal
		Log(Logs::General, Logs::Quests, "trigger %d not in journal", trigger_id); 
		return;
	}

	Integer_Map::iterator iter = pending_complete.find(quest_id);
	if (iter != pending_complete.end())
	{
		bool activity_complete = true;

		Log(Logs::General, Logs::Quests, "AC: completing pending %d",
			iter -> second);
		// a previous visible activity needs to complete
		send_activity_complete(quest_id, iter -> second, activity_complete);
	}

	Log(Logs::General, Logs::Quests, "AC: scheduling pending %d",
		trigger_id);

	// schedule this new trigger
	pending_complete[quest_id] = trigger_id;
}

void Client_State::send_visible_activity(int slot, int trigger)
{
	string task;
	int zone;
	int count;
	int done = trigger_state[trigger];
	int quest_id;

	if (!QuinceTriggerCache::Instance().quest_for(trigger, quest_id))
	{
		Log(Logs::General, Logs::Quests, "no quest for trigger %d", trigger);
		return;
	}

	if (quest_desc.find(quest_id) == quest_desc.end())
	{
		Log(Logs::General, Logs::Quests, "send_visible_activity: cannot update quest %d before it is sent to the client",
			quest_id);
		return;
	}
	if (!QuinceTriggerCache::Instance().get_task(trigger, task))
	{
		Log(Logs::General, Logs::Quests, "no task for trigger %d", trigger);
		return;
	}

	if (!QuinceTriggerCache::Instance().get_zone(trigger, zone))
	{
		Log(Logs::General, Logs::Quests, "no zone for trigger %d", trigger);
		return;
	}

	if (! QuinceTriggerCache::Instance().get_count(trigger, count))
	{
		Log(Logs::General, Logs::Quests, "no count for trigger %d", trigger);
		return;
	}

	if (!QuinceTriggerCache::Instance().is_boolean(trigger))
	{
		done = trigger_state[trigger];
	}
	else if (count == trigger_state[trigger])
	{
		done = 1;
		count = 1;
	}
	else
	{
		done = 0;
		count = 1;
	}

	int pending_id = pending_complete[quest_id];
	if (pending_id != -1)
	{
		bool activity_complete = true;
		Log(Logs::General, Logs::Quests, "AC: resolving pending %d",
			pending_id);
		send_activity_complete(quest_id, pending_id, activity_complete);
		pending_complete.erase(quest_id);
	}

	Log(Logs::General, Logs::Quests, "trigger %d count is %d", trigger,
		count);
	Log(Logs::General, Logs::Quests, "trigger %d seq %d slot is %d", trigger,
		quest_seq[quest_id], slot);
	Log(Logs::General, Logs::Quests, "zone is %d", zone);

	const int task_complete 	= 0xcaffffff;
	const int task_active 		= 0xffffffff;
	int seq = quest_seq[quest_id];
	const char *desc = task.c_str();		// may be ""

	int PacketLength = sizeof(TaskActivityHeader_Struct) 
		+ sizeof(TaskActivityData1_Struct) + sizeof(TaskActivityTrailer_Struct);
	// include two nulls for text1 and text2 (unused)
	PacketLength += strlen(desc) + 3;

	TaskActivityHeader_Struct *tah;
	TaskActivityData1_Struct *tad1;
	TaskActivityTrailer_Struct *tat;
	char *Ptr;

	EQApplicationPacket *outapp = new EQApplicationPacket(OP_TaskActivity,
		PacketLength);

	tah = (TaskActivityHeader_Struct *) outapp -> pBuffer;
	tah -> TaskSequenceNumber = seq;		// client slot
	tah -> unknown2 = 0x00000002;
	tah -> TaskID = quest_id;
	tah -> ActivityID = slot;
	tah -> unknown3 = 0x00000000;
	tah -> ActivityType = 9;				// controlled by quest manager
	tah -> Optional = 0;
	tah -> unknown5 = 0x00000000;

	Ptr = (char *) tah + sizeof(TaskActivityHeader_Struct);
	*Ptr++ = '\0';		// text1
	*Ptr++ = '\0';		// text2

	tad1 = (TaskActivityData1_Struct *) Ptr;

	tad1 -> GoalCount = count;
	tad1 -> unknown1 = 0xffffffff;
	tad1 -> unknown2 = task_active;

	tad1 -> ZoneID = zone;

	tad1 -> unknown3 = 0x00000000;

	Ptr = (char *) tad1 + sizeof(TaskActivityData1_Struct);
	sprintf(Ptr, "%s", desc);
	Ptr += strlen(desc) + 1;

	tat = (TaskActivityTrailer_Struct *) Ptr;
	tat -> DoneCount = done;
	tat -> unknown1 = 0x00000001;

	client -> QueuePacket(outapp);
	safe_delete(outapp);
}

// sent once per quest
void Client_State::send_task_description(int quest_id, bool show_journal)
{
	const int bring_up_journal 	= 0x00000201;
	const int no_journal 		= 0x00000200;
	const int xp_reward 		= 0x00000100;
	const int no_xp_reward 		= 0x00000000;

	if (quest_desc.find(quest_id) == quest_desc.end())
	{
		Log(Logs::General, Logs::Quests, "quest %d not present (use 145)",
			quest_id);
		client -> Message(0, "quest %d not present (try 145)", quest_id);
		return;
	}

	const char *desc = quest_desc[quest_id].c_str();
	const char *title = quest_title[quest_id].c_str();
	int seq = quest_seq[quest_id];

	int PacketLength = sizeof(TaskDescriptionHeader_Struct) + strlen(title) + 1
		+ sizeof(TaskDescriptionData1_Struct) + strlen(desc) + 1
		+ sizeof(TaskDescriptionData2_Struct)
		+ sizeof(TaskDescriptionTrailer_Struct);

	string RewardText = "Nothing";

	PacketLength += strlen(RewardText.c_str()) + 1;

	char *Ptr;
	TaskDescriptionHeader_Struct *tdh;
	TaskDescriptionData1_Struct *tdd1;
	TaskDescriptionData2_Struct *tdd2;
	TaskDescriptionTrailer_Struct *tdt;

	EQApplicationPacket *outapp = new EQApplicationPacket(OP_TaskDescription,
		PacketLength);
	tdh = (TaskDescriptionHeader_Struct *) outapp -> pBuffer;
	tdh -> SequenceNumber = seq;	// client slot
	tdh -> TaskID = quest_id;
	tdh -> open_window = show_journal;
	tdh -> task_type = 0;
	tdh -> reward_type = 0;

	Ptr = (char *) tdh + sizeof (TaskDescriptionHeader_Struct);
	sprintf(Ptr, "%s", title);
	Ptr += strlen(Ptr) + 1;

	tdd1 = (TaskDescriptionData1_Struct *) Ptr;
	tdd1 -> Duration = 0;		// unlimited
	tdd1 -> dur_code = 0x00000000;
	tdd1 -> StartTime = time(NULL);		// now

	Ptr = (char *) tdd1 + sizeof(TaskDescriptionData1_Struct);
	sprintf(Ptr, "%s", desc);
	Ptr += strlen(Ptr) + 1;

	tdd2 = (TaskDescriptionData2_Struct *) Ptr;
	tdd2 -> has_rewards = 1;
	tdd2 -> coin_reward = 1000;		// in copper
	tdd2 -> xp_reward = 0;			// 1 or 0
	tdd2 -> faction_reward = 0;		// 1 or 0

	Ptr = (char *) tdd2 + sizeof(TaskDescriptionData2_Struct);
	sprintf(Ptr, "%s", RewardText.c_str());
	Ptr += strlen(Ptr) + 1;

	tdt = (TaskDescriptionTrailer_Struct *) Ptr;
	tdt -> Points = 0;
	tdt -> has_reward_selection = 0;		// no rewards window

	client -> QueuePacket(outapp);
	safe_delete(outapp);
}

// act is the slot# for the step (client index)
void Client_State::send_activity_complete(int quest_id, int trigger_id, bool last)
{
	TaskActivityComplete_Struct *tac;

	EQApplicationPacket *outapp = new EQApplicationPacket(OP_TaskActivityComplete,
		sizeof(TaskActivityComplete_Struct));

	tac = (TaskActivityComplete_Struct *) outapp -> pBuffer;

	int seq = quest_seq[quest_id];

	// look up slot
	int slot;
	Integer_Map::const_iterator iter = journal_slots.find(trigger_id);
	if (iter == journal_slots.end())
	{
		Log(Logs::General, Logs::Quests,  "trigger %d not in journal map",
			trigger_id);
		return;
	}
	slot = iter -> second;

	tac -> TaskIndex = seq;
	tac -> TaskType = 2;			// quest, from TaskType in tasks.h
	tac -> TaskID = quest_id;
	tac -> ActivityID = slot;
	tac -> task_completed = 1;

	// from taskincomplete:  1 = more to come, 0 = last activity
	if (!last)
	{
		Log(Logs::General, Logs::Quests,  "quest complete");
		tac -> stage_complete = 0;
	}
	else
	{
		Log(Logs::General, Logs::Quests, "activity complete"); 
		tac -> stage_complete = 1;
	}


	client -> QueuePacket(outapp);
	safe_delete(outapp);
}

void Client_State::cancel_task(int quest_id)
{
	EQApplicationPacket* outapp = new EQApplicationPacket(OP_CancelTask,
		sizeof(CancelTask_Struct));
	CancelTask_Struct* cts = (CancelTask_Struct*) outapp->pBuffer;
	cts->SequenceNumber = quest_seq[quest_id];
	cts->type = 0x00000002;			// quest

	client -> QueuePacket(outapp);
	safe_delete(outapp);

	ID_List::iterator iter;

	iter = std::find(active_quests.begin(), active_quests.end(), quest_id);
	if (iter != active_quests.end())
	{
		active_quests.erase(iter);
	}

	// clean up other data structures
	ID_List nodes = QNodeCache::Instance().node_ids_for_quest(quest_id);

	Log(Logs::General, Logs::Quests, "client %d cancelling quest %d",
		character_id, quest_id);

	// note that nodes is a local list and will go away on its own
	for (iter = nodes.begin(); iter != nodes.end(); ++iter)
	{
		Log(Logs::General, Logs::Quests, "clean up node %d", *iter);
		int node_id = *iter;
		remove_active_triggers(node_id);		// also polled triggers
		node_seq.erase(node_id);
		required_triggers.erase(node_id);
	}

	Log(Logs::General, Logs::Quests, "clean up the rest of client state", *iter);
	quest_seq.erase(quest_id);
	quest_title.erase(quest_id);
	quest_desc.erase(quest_id);
	journal_slots.erase(quest_id);

	Log(Logs::General, Logs::Quests, "clean up modifications");
	QModifications::Instance().cancel_task(character_id, quest_id);
}

void Client_State::cancel_quest(int seq_id)
{
	Integer_Map::iterator iter;
	for (iter = quest_seq.begin(); iter != quest_seq.end(); ++iter)
	{
		if (iter -> second == seq_id)
		{
			cancel_task(iter -> first);
			return;
		}
	}

	Log(Logs::General, Logs::Quests, "cancel_quest: no seq_id %d", seq_id);
}

void Client_State::fail_quest(int quest_id)
{
	TaskActivityComplete_Struct *tac;

	EQApplicationPacket *outapp = new EQApplicationPacket(OP_TaskActivityComplete,
		sizeof(TaskActivityComplete_Struct));
	tac = (TaskActivityComplete_Struct *) outapp -> pBuffer;

	int seq = quest_seq[quest_id];
	tac -> TaskIndex = seq;
	tac -> TaskType = 2;
	tac -> TaskID = quest_id;
	tac -> ActivityID = 0;
	tac -> task_completed  = 0x00000000;	// Fail
	tac -> stage_complete  = 0;				// 0 for task complete or failed

	client -> QueuePacket(outapp);
	safe_delete(outapp);

	cancel_task(quest_id);
}

void Client_State::process()
{
	Mob *player = client -> CastToMob();

	Event_Proximity event;
	event.setZone(player -> GetZoneID());
	event.setXYZ(player -> GetX(), player -> GetY(), player -> GetZ());
	event.setDescriptor(Entity_Player, character_id);

	// check proximity triggers
	ID_List temp = polled_triggers;

	ID_List::iterator iter;
	
	for (iter = temp.begin(); iter != temp.end(); ++iter)
	{
		int trigger_id = *iter;
		Log(Logs::General, Logs::Quests, "process polled trigger %d", trigger_id); 
		QuinceTriggerCache::Instance().process(trigger_id, &event);
	}

	QuinceTriggerCache::Instance().process_global_activation(&event);
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
	Trigger_ID_Set::const_iterator trig_iter;
	Trigger_ID_Set to_fire;				// triggers to fire

	QuestEventID event_type = event -> GetType();


	// active_triggers is a multimap from node => trigger
	// attempt to fire on each trigger of the appropriate type
	for (trig_iter = active_triggers.begin(); trig_iter != active_triggers.end();
		++trig_iter)
	{
		int trigger_id = trig_iter -> second;

		QuestEventID type;
		if (! QuinceTriggerCache::Instance().getType(trigger_id, type))
		{
			Log(Logs::General, Logs::Quests, "no type for trigger %d", trigger_id);
			continue;
		}

		// trigger_type matches event?
		// trigger will refuse to fire if not active
		if (type == event_type)
		{
			to_fire.insert(pair<int,int>(0,trigger_id));
		}
		else
		{
			Log(Logs::General, Logs::Quests, "wrong type %d != %d",
				event_type, type);
		}
	}

	for (trig_iter = to_fire.begin(); trig_iter != to_fire.end(); ++trig_iter)
	{
		int trigger_id = trig_iter->second;

		QuinceTriggerCache::Instance().process(trigger_id, event);
	}

	// possible that there is an activation trigger for this event
	QuinceTriggerCache::Instance().process_global_activation(event);
}
