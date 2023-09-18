#include <algorithm>
#include "client.h"
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <iomanip>
#include <math.h>
#include "../common/string_util.h"		// for MakeAnyLenString
#include "../common/random.h"			// EQEMU::Random
#include "../common/misc_functions.h"	// FloatToEQ19
#include "../common/eqemu_logsys.h"		// for Log
#include "map.h"			// glm::vec3 (position.h) and map routines

#include "Quince_triggers.h"
#include "Quince_events.h"			// Event_Signal for one

#include "Quince.h"
#define LOG_CACHE	1

using namespace std;
string event_string(QuestEventID id);

Quince::Quince()
{
	registrations = new Entity_Registration;
}

unique_ptr<Quince> Quince::_instance;

Quince&
Quince::Instance()
{
    if (_instance.get() == NULL)
    {
        _instance.reset (new Quince);
    }

    return *_instance;
}

// ==============================================================
// delete_member -- a functor used to delete elements from lists/vectors
// ==============================================================

template <typename T>
void delete_member(T* const ptr)
{
	delete ptr;
}

// ==============================================================
// process_global_event -- process events not associated with a single character
//
// Some events (such as signals) occur independent of any client activity,
// and as a result do not have a client context.  This context is needed
// for the scripts associated with the triggers.
//
// Upon receipt of such an event, all clients are checked for an
// active trigger
// ==============================================================
void Quince::process_global_event(Event *event, int quest_id)
{
	Client_Map::iterator iter;

	Log(Logs::General, Logs::Quests, "global event in to Quince");

	for (iter = client_state.begin(); iter != client_state.end(); ++iter)
	{
		// send the event to each client:  let them decide if they need it
		Client_State &state = iter -> second;
		state.process_global_event(event, quest_id);
	}
}

Client *Quince::get_client(uint32 character_id)
{
	Client_Map::iterator iter = client_state.find(character_id);
	if (iter != client_state.end())
	{
		return iter -> second.get_client();
	}

	return NULL;
}

// ==============================================================
// debug routine to add a quest from the console
// adds quest by root-node
// ==============================================================
void Quince::test_add(Client *c, int root_node)
{
	stringstream ss;

	int character_id = c -> CharacterID();

	Client_Map::iterator iter = client_state.find(character_id);
	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "no client state!");
		c -> Message(0, "Quince:  no client state!");
		return;
	}

	client_state[character_id].start_quest(root_node);
}

// ==============================================================
// debug routine to fire a trigger -- note that this does not
// execute the Perl associated with the trigger
// ==============================================================
void Quince::test_fire(Client *c, int trigger_id)
{
	stringstream ss;

	int character_id = c -> CharacterID();

	Client_Map::iterator iter = client_state.find(character_id);
	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "no client state!");
		c -> Message(0, "Quince:  no client state!");
		return;
	}

	client_state[character_id].complete_trigger(trigger_id);
}

void Quince::test_comp(Client *c, int trigger_id)
{
	stringstream ss;

	int character_id = c -> CharacterID();

	Client_Map::iterator iter = client_state.find(character_id);
	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "no client state!");
		c -> Message(0, "Quince:  no client state!");
		return;
	}

	int quest_id;
	if (! QuinceTriggerCache::Instance().quest_for(trigger_id, quest_id))
	{
		Log(Logs::General, Logs::Quests, "no such trigger!");
		c -> Message(0, "Quince:  no such trigger");
		return;
	}

	client_state[character_id].send_activity_complete(quest_id, trigger_id, false);
}


// ==============================================================
// Reload client state on zone-in
// ==============================================================
void Quince::zone_in(Client *c, int zone)
{
	int character_id = c -> CharacterID();
	Log(Logs::General, Logs::Quests, "client %d zone in!", character_id);
	if (client_state.find(character_id) != client_state.end())
	{
		Log(Logs::General, Logs::Quests, "already see a client state for char %d", character_id);
		return;
	}

	client_state[character_id].load_state (c, zone);
}

// ==============================================================
// and clean up on zone-out
// ==============================================================
void Quince::zone_out(Client *c)
{
	Log(Logs::General, Logs::Quests, "client zone out!");
	int character_id = c -> CharacterID();
	Client_Map::iterator iter = client_state.find(character_id);
	if (iter != client_state.end())
	{
		iter -> second.unload();	 // will save state
		client_state.erase(iter);
		Log(Logs::General, Logs::Quests, "deleted client state");
	}

	reset(character_id);
#if 0
	QuinceTriggers::Instance().reset();
	QuinceTriggerCache::Instance().reset();
	QNodeCache::Instance().reset();
#endif
}

void Quince::process()
{
	Client_Map::iterator iter = client_state.begin();

	while (iter != client_state.end())
	{
		iter -> second.process();
		++iter;
	}

	process_timers();
}

// ==============================================================
// debug routine to display nodes and triggers (testing cache)
// ==============================================================
void Quince::list(Client *c)
{
	int num_nodes = QNodeCache::Instance().num_nodes();
	int num_triggers = QuinceTriggerCache::Instance().num_triggers();
	int num_act = QuinceTriggerCache::Instance().activation_zones();

	stringstream ss;
	ss << num_nodes << " nodes in the cache, " << num_triggers << " triggers";
	c -> Message(0, ss.str().c_str());

	ss.str("");
	ss << num_act << " zone(s) with activation triggers";
	c -> Message(0, ss.str().c_str());

	QNodeCache::Instance().display(c);
	QuinceTriggerCache::Instance().display(c);
}

// ==============================================================
// debug routine to bring up the task selector for a quest
// ==============================================================
void Quince::show_selector(Client *c, int quest_id)
{
	int num_tasks = 1;
	const char *Title = "A Friend in Need...";
	const char *Desc = "Check on Farmer Jones' Friend";			// does not need to show structure


	int PacketLength = sizeof(AvailableTaskHeader_Struct) +
		num_tasks * (sizeof(AvailableTaskData1_Struct) + sizeof(AvailableTaskData2_Struct) +
		10 + strlen(Title) + 1 + strlen(Desc) + 1 + sizeof(AvailableTaskTrailer_Struct) + 5);

	EQApplicationPacket* outapp = new EQApplicationPacket(OP_OpenNewTasksWindow, PacketLength);
	char *Ptr;

	Log(Logs::General, Logs::Quests, "bringing up new selector, len %d", PacketLength);

	AvailableTaskHeader_Struct* Header;
	AvailableTaskData1_Struct* Data1;
	AvailableTaskData2_Struct* Data2;
	AvailableTaskTrailer_Struct* Trailer;

	Header = (AvailableTaskHeader_Struct*)outapp->pBuffer;

	Header -> TaskCount = num_tasks;
	Header -> unknown1 = 2;
	Header -> TaskGiver = c->GetID();		// 23

	Ptr = (char*) Header + sizeof(AvailableTaskHeader_Struct);

	Data1 = (AvailableTaskData1_Struct*) Ptr;
	Data1 -> TaskID = quest_id;		// id in task table
	Data1 -> TimeLimit = 0;		// unlimited
	Data1 -> unknown2 = 0;

	Ptr = (char*) Data1 + sizeof(AvailableTaskData1_Struct);
	sprintf(Ptr, "%s", Title);
	Ptr = Ptr + strlen(Ptr) + 1;

	sprintf(Ptr, "%s", Desc);
	Ptr = Ptr + strlen(Ptr) + 1;

	Data2 = (AvailableTaskData2_Struct*) Ptr;

	Data2 -> unknown1 = 1;
	Data2 -> unknown2 = 0;
	Data2 -> unknown3 = 1;
	Data2 -> unknown4 = 0;

	Ptr = (char *) Data2 + sizeof(AvailableTaskData2_Struct);
	sprintf(Ptr, "ABCD");
	Ptr = Ptr + strlen(Ptr) + 1;
	sprintf(Ptr, "ABCD");
	Ptr = Ptr + strlen(Ptr) + 1;

	Trailer = (AvailableTaskTrailer_Struct *) Ptr;

	Trailer -> ItemCount = 1;
	Trailer -> unknown1 = 0xffffffff;
	Trailer -> unknown2 = 0xffffffff;
	Trailer -> StartZone = 395;

	Ptr = (char *) Trailer + sizeof(AvailableTaskTrailer_Struct);

	sprintf(Ptr, "ABCD");
	Ptr = Ptr + strlen(Ptr) + 1;

	c -> QueuePacket(outapp);
	safe_delete(outapp);
}

// ==============================================================
// Send quest information to the client
//
// This effectively clears the quest journal entries for this quest
// allowing new activities to be introduced.
//
// Client_State::send_task_state calls this, since it is common to
// want a clean state before resending activities
// ==============================================================
void Quince::send_task(Client *c, int quest_id)
{
	int character_id = c -> CharacterID();

	Log(Logs::General, Logs::Quests, "******* Quince send quest %d to client ******* ", quest_id);

	Client_Map::iterator iter = client_state.find(character_id);
	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "no client state!");
		c -> Message(0, "Quince:  no client state!");
		return;
	}

	iter -> second.send_task(quest_id);
}

// ==============================================================
// Assign a quest to the player -- called from client_packet.cpp when
// an AcceptNewTask op-code is received
// 
// Returns:  True if the quest is successfully started.
// ==============================================================
bool Quince::accept_quest(Client *c, int quest_id, int npc_id)
{
	int character_id = c -> CharacterID();

	Log(Logs::General, Logs::Quests, "accept quest %d", quest_id);

	Client_Map::iterator iter = client_state.find(character_id);

	if (iter != client_state.end())
	{
		int root_node = get_root_node(quest_id);

		if (root_node < 0)
		{
			Log(Logs::General, Logs::Quests, "no root node for quest");
			return false;
		}

		Log(Logs::General, Logs::Quests, "attempt to start quest at node %d", root_node);
		return iter -> second.start_quest(root_node);
	}
	else
	{
		Log(Logs::General, Logs::Quests, "no client state for char");
	}

	return false;
}

void Quince::cancel_quest(Client *c, int seq_id)
{
	int character_id = c -> CharacterID();

	Log(Logs::General, Logs::Quests, "cancel quest journal slot %d", seq_id);

	Client_Map::iterator iter = client_state.find(character_id);

	if (iter != client_state.end())
	{
		iter -> second.cancel_quest(seq_id);
	}
	else
	{
		Log(Logs::General, Logs::Quests, "no client state");
	}
}


void Quince::fail_quest(Client *c, int quest_id)
{
	int character_id = c -> CharacterID();
	Log(Logs::General, Logs::Quests, "fail quest %d", quest_id);
	Client_Map::iterator iter = client_state.find(character_id);

	if (iter != client_state.end())
	{
		iter -> second.fail_quest(quest_id);
	}
	else
	{
		Log(Logs::General, Logs::Quests, "no client state");
	}
}

// ==============================================================
// Return the root node for a quest (from the database).  Needed
// when a quest is added, since we don't keep this info around.  Plus
// it is unlikely that this same quest has been added recently.
// ==============================================================
int Quince::get_root_node(int quest_id)
{
    const char *error_msg = "[QUINCE] error loading root node";

    char errbuf[MYSQL_ERRMSG_SIZE];

    int node_id = -1;

    std::string query = StringFormat("SELECT node_id FROM `quince_triggers`"
						" WHERE quest_id=%d AND seq=0", quest_id);
    auto results = database.QueryDatabase(query);

    if (results.Success())
    {
		auto row = results.begin();

        node_id = atoi(row[0]);
    }
    else
    {
		Log(Logs::General, Logs::Quests, error_msg, errbuf);
    }

    return node_id;
}

bool Quince::get_quest_title(int quest_id, string& title)
{
	std::string query = StringFormat("SELECT title FROM `quince_quests` WHERE id=%d", quest_id);

	auto results = database.QueryDatabase(query);

	if (! results.Success() || (results.RowCount() == 0))
	{
		Log(Logs::General, Logs::Quests, "Error loading title from DB: %s", results.ErrorMessage());
		return false;
	}

	auto row = results.begin();
	title = row[0];
	return true;
}

// ==============================================================
// Debug routine to dump client state
// ==============================================================
void Quince::show_client(int client_id)
{
	Client_Map::const_iterator iter = client_state.find(client_id);

	if (iter != client_state.end())
	{
		iter -> second.display();
	}
}

// ==============================================================
// Debug routine to activate a spawn group.
//
// The spawngroup respawns as normal
// ==============================================================
void Quince::start_spawn(unsigned int spawngroup)
{
	LinkedListIterator<Spawn2*> iterator(zone->spawn2_list);

	Log(Logs::General, Logs::Quests, "start spawngroup %d", spawngroup);

	iterator.Reset();
	while (iterator.MoreElements())
	{
		Spawn2 *point = iterator.GetData();
		if (point -> SpawnGroupID() == spawngroup)
		{
			point -> Enable();
			point -> Reset();
			point -> Repop();
		}

		iterator.Advance();
	}
}

// ==============================================================
// Debug routine to deactivate a spawn group
// ==============================================================
void Quince::stop_spawn(unsigned int spawngroup)
{
	LinkedListIterator<Spawn2*> iterator(zone->spawn2_list);

	Log(Logs::General, Logs::Quests, "despawn all in spawngroup %d", spawngroup);

	iterator.Reset();
	while (iterator.MoreElements())
	{
		Spawn2 *point = iterator.GetData();
		if (point -> SpawnGroupID() == spawngroup)
		{
			point -> Disable();
		}

		iterator.Advance();
	}
}

void Quince::spawn_nearby(int char_id, int quest_id, int eset_id, uint32 spawngroup, float x, float y, float z, uint32 count)
{
#if 0
	// FIXME -- pathing change makes this obsolete

	if (!zone->pathing)
	{
		Log(Logs::General, Logs::Quests, "spawn_nearby: need pathing info for zone");
		return;
	}

	SpawnGroup *sg =  zone -> spawn_group_list.GetSpawnGroup(spawngroup);
	if (sg == NULL)
	{
		Log(Logs::General, Logs::Quests, "spawn_nearby: unable to locate spawn group %d", spawngroup);
		return;
	}

	glm::vec3 center(x,y,z);

	int path_node = zone -> pathing -> FindNearbyNonLOS(center);
	if (path_node < 0)
	{
		Log(Logs::General, Logs::Quests, "spawn_nearby: could not find nearby non-los");
		return;
	}

	glm::vec3 spawnpoint = zone -> pathing -> GetPathNodeCoordinates(path_node);
	Log(Logs::General, Logs::Quests, "spawn at pathnode %d (%f,%f,%f)",
		path_node, spawnpoint.x, spawnpoint.y, spawnpoint.z);


	for (int i = 0; i < count; i++)
	{
		// FIXME:  randomly offset these points later
		int32 npcid = sg -> GetNPCType();
		if (npcid == 0)
		{
			Log(Logs::General, Logs::Quests, "spawn_nearby: no npc from spawngroup %d",
				spawngroup);
			return;
		}
		glm::vec4 point(spawnpoint.x, spawnpoint.y, spawnpoint.z, 0);
		Mob *mob = quest_manager.spawn2(npcid, 0, 0, point);
		NPC *npc = mob -> CastToNPC();
		int eid = mob -> GetID();
		npc -> SetSp2(spawngroup);
		QModifications::Instance().add_to_eset(char_id, eset_id, 
			Entity_NPC, eid);
		Log(Logs::General, Logs::Quests, "spawn nearby:  spawn %d", eid);
	}
#endif
}

// ==============================================================
// Timer support
//
// Timers generate a quest-unique signal upon expiration.  Prior
// to expiring a timer may be stopped and restarted.
//
// It is expected that triggers will be created to handle the
// signals.  If no such trigger exists, the signal is ignored.
//
// If a timer is currently running, a subsequent call to start_timer
// will replace the timer state.  This could be used for watchdog timers.
//
// The Timer_Map is used to speed up search for the timer.
// ==============================================================

void Quince::start_timer(uint32 char_id, uint32 quest, uint32 signal, uint32 time)
{
	Timer_List::iterator iter;

	for (iter = active_timers.begin(); iter != active_timers.end(); ++iter)
	{
		Timer_Binding *active = *iter;

		if ((active -> quest == quest) && (active -> signal == signal))
		{
			active -> timer.SetTimer(time);
			return;
		}
	}

	Timer_Binding *binding = new Timer_Binding(quest, signal);
	binding -> timer.SetTimer(time);
	binding -> character_id = char_id;
	active_timers.push_back(binding);
}

void Quince::stop_timer(uint32 char_id, uint32 quest, uint32 signal)
{
	Timer_List::iterator iter;
	Timer_List to_destroy;

	// build a list of timer bindings to delete
	// we cannot delete them while iterating through the list
	for (iter = active_timers.begin(); iter != active_timers.end(); ++iter)
	{
		Timer_Binding *active = *iter;

		if (active -> character_id != char_id)
		{
			continue;
		}

		if ((active -> quest == quest) && (active -> signal == signal))
		{
			active -> timer.Disable();
			to_destroy.push_back(active);
		}
	}

	// walk the to-destroy list and remove each timer
	for (iter = to_destroy.begin(); iter != to_destroy.end(); ++iter)
	{
		active_timers.remove(*iter);
	}
}

// ==============================================================
// Stop (and remove) all timers for a character
//
// Useful when a character leaves the zone with a quest active
// ==============================================================
void Quince::stop_all_timers(uint32 char_id)
{
	Timer_List::iterator iter;

	for (iter = active_timers.begin(); iter != active_timers.end();)
	{
		Timer_Binding *active = *iter;

		if (active -> character_id != char_id)
		{
			++iter;
			continue;
		}

		iter = active_timers.erase(iter);
	}
}


// ==============================================================
// check all timers and generate global events for any that
// have expired.  Timers disable upon expiration (are not reset)
// ==============================================================
void Quince::process_timers()
{
	Timer_List::iterator iter;
	Timer_List to_run;

	for (iter = active_timers.begin(); iter != active_timers.end();)
	{
		Timer_Binding *active = *iter;

		if (active -> timer.Check())
		{
			to_run.push_back(active);
		}
		else
		{
			++iter;
		}
	}

	for (iter = to_run.begin(); iter != to_run.end(); ++iter)
	{
		Timer_Binding *active = *iter;
	
		// timer may have been disabled by prior signal
		if (! active -> timer.Enabled())
		{
			continue;
		}

		Event_Signal  event;
		event.setSignalID(active -> signal);

		process_global_event(&event, active -> quest);
	}
}

void Quince::send_signal(uint32 char_id, uint32 quest, uint32 signal)
{
	Event_Signal event;
	event.setSignalID (signal);

	Client_Map::iterator iter = client_state.find(char_id);

	if (iter != client_state.end())
	{
		Client_State &state = iter -> second;
		state.process_global_event(&event, quest);
	}
}


// ==============================================================
// Transient variable management
//
// Created to allow inter-trigger communication.  For example disabling a
// proxy NPC under different conditions.
// ==============================================================

void Quince::store_value(uint32 char_id, uint32 quest, char *key, int value)
{
	Variable_Binding binding;
	binding.quest = quest;
	binding.character_id = char_id;
	binding.key = key;
	binding.value = value;

	variables[char_id][key] = binding;
}

int Quince::retrieve_value(uint32 char_id, uint32 quest, char *key)
{
	Var_Map::const_iterator iter = variables.find(char_id);
	if (iter == variables.end())
	{
		Log(Logs::General, Logs::Quests, "no variable %s for char %d", key, char_id);
		return 0;
	}

	Var_Pool::const_iterator iter2 = iter -> second.find(key);
	for (iter2 = iter -> second.begin(); iter2 != iter -> second.end(); ++iter2)
	{
		if (iter2 -> second.quest != quest)
		{
			continue;
		}

		if (iter2 -> second.key.compare(key) == 0)
		{
			return iter2 -> second.value;
		}
	}

	Log(Logs::General, Logs::Quests, "access unbound variable %s", key);
	return 0;
}

// ==============================================================
// remove all variable bindings for a character
// useful when a character zones out
// ==============================================================
void Quince::remove_all_vars(uint32 char_id)
{
	Var_Map::iterator iter = variables.find(char_id);
	if (iter != variables.end())
	{
		iter -> second.clear();
	}
}

// ==============================================================
// Quest-manager side of trigger firing.  Update quest state, and notify
// the client of any changes.
//
// A trigger may be active for more than one character, so multiple clients
// may be updated.
//
// FIXME:  some triggers may require proximity before a character can complete
// them.  We don't want a quest updating when a character on the other side of the
// zone talks to an NPC.
// ==============================================================
void Quince::notify_clients(int trigger_id, int character_id)
{
	Log(Logs::General, Logs::Quests, "QM notify trig %d, char %d", trigger_id,
		character_id);
	if (character_id != 0)
	{
		Client_Map::iterator iter = client_state.find(character_id);

		if (iter != client_state.end())
		{
			Log(Logs::General, Logs::Quests, "QM completing %d", trigger_id); 
			iter -> second.complete_trigger(trigger_id);
		}
		return;
	}

	ID_List clients = QuinceTriggerCache::Instance().associated_clients(trigger_id);

	// character-id 0 means all associated clients
	ID_List::const_iterator iter = clients.begin();
	while(iter != clients.end())
	{
		Log(Logs::General, Logs::Quests, "checking client %d", *iter);

		Client_Map::iterator client_iter = client_state.find(*iter);
		if (client_iter != client_state.end())
		{
			Log(Logs::General, Logs::Quests, "fire trigger for char %d", character_id);
			client_iter -> second.complete_trigger(trigger_id);
		}

		++iter;
	}
}

// ==============================================================
// Indicate whether a specific character is associated with a trigger
// (waiting on trigger to fire).  QuinceTriggers checks association before
// completing them.
//
// Activation triggers are always treated as associated, unless the character
// already has the quest.
// ==============================================================
bool Quince::is_associated(int trigger_id, int character_id)
{
	Client_Map::iterator iter = client_state.find(character_id);

	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "character %d not found", character_id);
		return false;
	}

	/*
	bool activation_trigger = 
			QuinceTriggerCache::Instance().is_activation_trigger(trigger_id);
	*/

	int quest_id;
	if (! QuinceTriggerCache::Instance().quest_for(trigger_id, quest_id))
	{
		// if we cannot determine the quest, then it isn't assoc
		Log(Logs::General, Logs::Quests, "quest not found for %d", trigger_id);
		return false;
	}

	bool has_quest = iter -> second.has_quest(quest_id);

	if ((QuinceTriggerCache::Instance().is_activation_trigger(trigger_id)) &&
			(! has_quest))
	{
		Log(Logs::General, Logs::Quests, "activation trigger for new quest");
		return true;
	}

	int zone_id;
	if (! QuinceTriggerCache::Instance().get_zone(trigger_id, zone_id))
	{
		Log(Logs::General, Logs::Quests, "no zone for trig %d", trigger_id);
		return false;
	}

	// zone_id of 0 means all zones
	if ((zone -> GetZoneID() != (unsigned int) zone_id) && (zone_id != 0))
	{
		Log(Logs::General, Logs::Quests, "skip out of zone trigger");
		return false;
	}

	// other triggers must be associated with character
	ID_List clients = QuinceTriggerCache::Instance().associated_clients(trigger_id);

	bool associated = std::find(clients.begin(), clients.end(), character_id) !=
		clients.end();

	return associated;
}

// ==============================================================
// Access trigger state
// ==============================================================

int Quince::get_trigger_state(int trigger_id, int character_id)
{
	Client_Map::iterator iter = client_state.find(character_id);

	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "get_trigger_state: character %d not found", 
			character_id);
		return 0;
	}

	Client_State &state = iter -> second;
	return state.get_trigger_state(trigger_id);
}

void Quince::set_trigger_state(int trigger_id, int character_id,
		int new_state)
{
	// validate character_id
	Client_Map::iterator iter = client_state.find(character_id);

	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, "get_trigger_state: character %d not found", 
			character_id);
		return;
	}

	Client_State &state = iter -> second;
	state.set_trigger_state(trigger_id, new_state);
}

bool Quince::is_trigger_satisfied(int trigger_id, int character_id)
{
	Client_Map::iterator iter = client_state.find(character_id);

	if (iter == client_state.end())
	{
		Log(Logs::General, Logs::Quests, 
			"is_trigger_satisfied: character %d not found", 
			character_id);
		return true;
	}

	Client_State &state = iter -> second;
	return state.is_satisfied(trigger_id);
}


// ==============================================================
// Need to reset state when zone is reset
// ==============================================================
void Quince::reset(uint32 character_id)
{
	Client_Map::iterator client_iter = client_state.find(character_id);
	if (client_iter != client_state.end())
	{
		client_iter -> second.unload();
		client_state.erase(client_iter);
	}

	stop_all_timers(character_id);
	remove_all_vars(character_id);
}

void Quince::register_watch(QuestEventID type, uint32 npc_id, uint32 char_id)
{
	registrations -> insert(type, npc_id, char_id);
}

void Quince::remove_watch(QuestEventID type, uint32 npc_id, uint32 char_id)
{
	registrations -> remove(type, npc_id, char_id);
}

bool Quince::check_watch(QuestEventID type, uint32 npc_id)
{
	return (registrations -> present(type, npc_id));
}

ID_List Quince::clients_for(QuestEventID type, uint32 entity_id)
{
	return (registrations -> clients_for(type, entity_id));
}

void Quince::restrict_item_target(int char_id, int quest_id, int item_id,
	int eset_id)
{
	// FIXME
}

void Quince::remove_item_restrict(int char_id, int quest_id, int item_id)
{
	// FIXME
}

// ==============================================================
// loot exchange
// ==============================================================
void Quince::exchange_loot(int char_id,  int quest_id, int eset_id,
		int orig_item, int new_item, int chance)
{
	Log(Logs::General, Logs::Quests, 
		"exchange loot:  char %d, quest_id %d, eset %d, item1 %d, item2 %d, chance %d",
		char_id, quest_id, eset_id, orig_item, new_item, chance);

	QModifications::Instance().insert_loot_mod(char_id, quest_id, eset_id,
		orig_item, new_item, chance);

	// loot_mods -> insert(char_id, quest_id, mset_id, orig_item, new_item, chance);
}

void Quince::remove_exchange(int char_id, int quest_id, int mset_id)
{
	// FIXME
}

// ==============================================================
// node rollback
// ==============================================================

void Quince::set_rollback(int char_id, int node_id, int offset)
{
	Client_Map::iterator iter = client_state.find(char_id);

	if (iter != client_state.end())
	{
		iter->second.set_rollback(node_id, offset);
	}
}

void Quince::clear_rollback(int char_id, int node_id)
{
	Client_Map::iterator iter = client_state.find(char_id);

	if (iter != client_state.end())
	{
		iter->second.clear_rollback(node_id);
	}
}

void Quince::init_waypoints(int char_id, int npc_eid)
{
	Client_Map::iterator iter = client_state.find(char_id);

	Log(Logs::General, Logs::Quests, "init waypoints client %d, entity  %d",
		char_id, npc_eid);

	if (iter != client_state.end())
	{
		iter->second.init_waypoints(npc_eid);
	}
}

NPC_WPs Quince::get_client_waypoint_info(int npc_eid)
{
	NPC_WPs results;

	Client_Map::iterator iter;
	for (iter = client_state.begin(); iter != client_state.end(); ++iter)
	{
		NPC_Waypoint_State state;
		if (iter -> second.get_waypoint_state(npc_eid, state))
		{
			results.push_back(state);
		}
	}

	if (results.size() != 0)
	{
		Log(Logs::General, Logs::Quests, "%d waypoint state entries for entity %d",
			results.size(), npc_eid);
	}

	return results;
}

void Quince::remove_client_waypoint_info(int npc_eid)
{
	Client_Map::iterator iter;
	for (iter = client_state.begin(); iter != client_state.end(); ++iter)
	{
		iter -> second.remove_waypoint_state(npc_eid);
	}
}

unique_ptr<QNodeCache> QNodeCache::_instance;

QNodeCache&
QNodeCache::Instance()
{
	if (_instance.get() == NULL)
	{
		_instance.reset (new QNodeCache);
	}

	return *_instance;
}

// ==============================================================
// Need something in order to make constructor protected
// ==============================================================
QNodeCache::QNodeCache()
{
}

void QNodeCache::reset()
{
	nodes.clear();
}

// ==============================================================
// determine if a node is in the cache
// ==============================================================
bool QNodeCache::node_present(int node_id)
{
	Node_Map::const_iterator iter = nodes.find(node_id);

	if (iter == nodes.end())
	{
		return false;
	}
	else
	{
		return true;
	}
}

// ==============================================================
// determine the quest for a node
// ==============================================================
bool QNodeCache::quest_for(int node_id, int& quest_id)
{
	Node_Map::const_iterator iter = nodes.find(node_id);

	if (iter != nodes.end())
	{
		quest_id = iter -> second.getQuest();
		return true;
	}

	return false;
}

// ==============================================================
// not in cache, load info from database
// ==============================================================
bool QNodeCache::quest_from_db(int node_id, int& value)
{
	std::string query = StringFormat("SELECT quest_id FROM `quince_questnodes` WHERE id=%d", node_id);

    int quest_id = -1;

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error loading quest from database: %s",
			results.ErrorMessage());
		return false;
	}

	auto row = results.begin();
	value = atoi(row[0]);
	return true;
}

// ==============================================================
// determine the name for a node
// ==============================================================
bool QNodeCache::getName(int node_id, std::string& value)
{
	Node_Map::const_iterator iter = nodes.find(node_id);

	if (iter != nodes.end())
	{
		value = iter->second.getName();
		return true;
	}

	return false;
}

// ==============================================================
// load a node into the cache, or increment its reference count if present
// ==============================================================
void QNodeCache::load_node(int node_id)
{
	Node_Map::iterator iter = nodes.find(node_id);

	if (iter != nodes.end())
	{
		iter -> second.add_reference();
		return;
	}

	int quest_id;
	if (! quest_for(node_id, quest_id))
	{
		if (! quest_from_db(node_id, quest_id))
		{
			Log(Logs::General, Logs::Quests, "node %d not found in database", 
				node_id);
			return;
		}
	}

    // one reference added upon create
    nodes.insert(pair<int, Quest_Node>
        (node_id, Quest_Node(quest_id, node_id)));
}

// ==============================================================
// remove a reference to a node, and remove it when 0 references remain
// ==============================================================
void QNodeCache::unload_node(int node_id)
{
	Node_Map::iterator iter = nodes.find(node_id);

	if (iter != nodes.end())
	{
		if (iter -> second.remove_reference() == 0)
		{
			nodes.erase(iter);
		}
		return;
	}

	Log(Logs::General, Logs::Quests, "attempt to unload non-present node %d", node_id);
}

// ==============================================================
// return a vector of trigger ids for a sequence
//
// This is for debugging, and to allow Client_State to activate these
// triggers for a client.
// ==============================================================
ID_List QNodeCache::trigger_ids_at_seq(int node_id, int seq)
{
	Node_Map::iterator iter = nodes.find(node_id);

	if (iter == nodes.end())
	{
		ID_List ids;
		return ids;
	}

	return iter->second.trigger_ids_at_seq(seq);
}

// ==============================================================
// return a vector of node ids for a quest
//
// Used for cancelling a quest
// ==============================================================
ID_List QNodeCache::node_ids_for_quest(int quest_id)
{
	Node_Map::iterator iter;
	ID_List node_ids;

	// search node map for nodes belong with quest
	for (iter = nodes.begin(); iter != nodes.end(); ++iter)
	{
		if (iter -> second.getQuest() == quest_id)
		{
			node_ids.push_back(iter -> first);
		}
	}

	return node_ids;
}

// ==============================================================
// return the parent trigger, if one exists
//
// Nodes that are subquests of another node will have a parent trigger
// (for the subquest step).  Firing this trigger signals that the subquest
// has completed.
// ==============================================================
bool QNodeCache::get_parent_trigger(int node_id, int& trigger_id)
{
	Node_Map::iterator iter = nodes.find(node_id);
	if (iter == nodes.end())
	{
		Log(Logs::General, Logs::Quests, "get_parent_trigger: no node %d in cache",
			node_id);
		return false;
	}

	int id = iter -> second.getParent();
	if (id == -1)
	{
		Log(Logs::General, Logs::Quests, "get_parent_trigger: no parent trigger for %d",
			node_id);
		return false;
	}

	trigger_id = id;
	return true;
}

void QNodeCache::display(Client *client) const
{
	stringstream ss;
	Node_Map::const_iterator iter;

#if LOG_CACHE
	Log(Logs::General, Logs::Quests, "Node Cache:");
#endif

	client -> Message(0, "Node Cache:");

	for (iter = nodes.begin(); iter != nodes.end(); ++iter)
	{
		ss << "node " << iter->second.getName() << " (" << iter->second.getID() <<
			") " << iter->second.num_references() << " refs,  parent trigger: " <<
			iter -> second.getParent();

#if LOG_CACHE
		Log(Logs::General, Logs::Quests, ss.str().c_str());
#endif
		client -> Message(0, ss.str().c_str());
		ss.str("");
	}
}

// ==============================================================
// Trigger cache -- for easy lookup of trigger information
// 
// Note that triggers have system-unique ids, so we don't need to know which
// quest they belong to
// ==============================================================

unique_ptr<QuinceTriggerCache> QuinceTriggerCache::_instance;

QuinceTriggerCache&
QuinceTriggerCache::Instance()
{
	if (_instance.get() == NULL)
	{
		_instance.reset (new QuinceTriggerCache);
	}

	return *_instance;
}

// ==============================================================
// need something here in order to make constructor protected
// ==============================================================
QuinceTriggerCache::QuinceTriggerCache ()
{
	Log(Logs::General, Logs::Quests, "Trigger cache created");
}

void QuinceTriggerCache::reset()
{
	// FIXME:  should remove assets associated with a single client
	Log(Logs::General, Logs::Quests, "Trigger cache reset");

	triggers.clear();
	node_ids.clear();
	ref_counts.clear();
	sequence.clear();
	zone_map.clear();
	trigger_owner.clear();
	activated_quest.clear();
	activation_triggers.clear();
	activation_ids.clear();
	polled_triggers.clear();
}

bool QuinceTriggerCache::node_for(int trigger_id, int& node_id)
{
	Integer_Map::const_iterator iter = node_ids.find(trigger_id);

	if (iter != node_ids.end())
	{
		node_id = iter -> second;
		return true;
	}

	return false;
}

// ==============================================================
// unload all triggers for a zone -- useful when shutting down a zone
//
// Hopefully as other assets are released, the set of active triggers would
// reduce to the activation triggers in the zone.  Rather than keeping track of
// those and risk missing a trigger, we can remove all of the triggers towards the
// end of the shutdown process.
// ==============================================================

void QuinceTriggerCache::unload_all_triggers(int zone)
{
	Log(Logs::General, Logs::Quests, 
		"TriggerCache:  unload all triggers for zone %d", zone);

	// obtain the set of triggers for this zone
	pair<Integer_Multimap::iterator, Integer_Multimap::iterator> trig_range =
		zone_map.equal_range(zone);

	vector<int> ids_to_remove;

	// build a list of trigger-ids, since info is stored in multiple containers
	// (the iterators will become invalid once we start deleting things)

	Integer_Multimap::iterator iter;
	for (iter = trig_range.first; iter != trig_range.second; ++iter)
	{
		// iter -> first is the zone-id
		// iter -> second is one of the trigger-ids in this zone
		ids_to_remove.push_back(iter -> second);
	}

	// we can clear out the zone map in one call
	zone_map.erase(trig_range.first, trig_range.second);

	for (unsigned int i = 0; i < ids_to_remove.size(); i++)
	{
		int id = ids_to_remove[i];

		// remove from the trigger map
		Trigger_Map::iterator trig_iter = triggers.find(id);
		if (trig_iter != triggers.end())
		{
			triggers.erase(trig_iter);
		}

		// remove from node-ids
		Integer_Map::iterator int_iter = node_ids.find(id);
		if (int_iter != node_ids.end())
		{
			node_ids.erase(int_iter);
		}

		int_iter = ref_counts.find(id);
		if (int_iter != ref_counts.end())
		{
			ref_counts.erase(int_iter);
		}
	}
}

// ==============================================================
// load a trigger into the cache, or ignore request if already present
//
// ==============================================================

void QuinceTriggerCache::load_trigger(int trigger_id, int seq, int character_id)
{
	Integer_Map::iterator iter = ref_counts.find(trigger_id);

	if (iter != ref_counts.end())
	{
		iter -> second++;
		return;
	}

	QuinceTrigger *trigger = QuinceTriggers::Instance().load_trigger(trigger_id);

	if (trigger == NULL)
	{
		Log(Logs::General, Logs::Quests, "failed to load trigger %d", trigger_id);
		return;
	}

	triggers[trigger_id] = trigger;
	node_ids[trigger_id] = trigger -> get_node();
	ref_counts[trigger_id] = 1;
	sequence[trigger_id] = seq;

	trigger_owner.insert(pair<int,int>(trigger_id, character_id));

	int zone = trigger -> getZone();

	zone_map.insert(pair<int,int>(zone, trigger_id));

	initialize_trigger(trigger);

	Log(Logs::General, Logs::Quests, "loaded trigger %d (%s)", trigger_id,
		trigger -> getName().c_str());
	Log(Logs::General, Logs::Quests, "trigger type = %s", 
			event_string(trigger -> getType()).c_str());
}

void QuinceTriggerCache::initialize_trigger(QuinceTrigger *trigger)
{
#if 0
	PerlembParser *parser = QuinceTriggers::Instance().getParser();
	Embperl *perl = parser -> getperl();

	const char *package_name = trigger -> packageName().c_str();
	ostringstream subroutine;
	subroutine << trigger -> getName() << "_init";
	int trigger_id = trigger -> getTriggerID();

	if (perl -> SubExists(package_name, subroutine.str().c_str()))
	{  
		parser->ExportVar(package_name, "trigger_id", trigger_id);

		Log(Logs::General, Logs::Quests, "trying to run %s -- %s",
			package_name, subroutine.str().c_str());
		parser -> SendCommands(package_name, subroutine.str().c_str(), 0, NULL,
			NULL, NULL);
	}
	else
	{
		Log(Logs::General, Logs::Quests, "no init subroutine %s",
			subroutine.str().c_str());
	}
#endif
}

// ==============================================================
// ==============================================================
void QuinceTriggerCache::unload_trigger(int trigger_id, int character_id)
{
	Integer_Map::iterator ref_iter = ref_counts.find(trigger_id);
	if (ref_iter == ref_counts.end())
	{
		Log(Logs::General, Logs::Quests, "unload: no trigger %d in cache",
			trigger_id);
		remove_trigger_owner(trigger_id, character_id);
		return;
	}

	ref_counts[trigger_id]--;
	Log(Logs::General, Logs::Quests, "TC: unload_trigger ref count now %d", 
		ref_counts[trigger_id]);

	if (ref_counts[trigger_id] > 0)
	{
		Log(Logs::General, Logs::Quests, "unload: reduced ref count");
		return;
	}

	Log(Logs::General, Logs::Quests, "unload: ref count now zero");

	int zone = -1;
	Trigger_Map::iterator trig_iter = triggers.find(trigger_id);
	if (trig_iter != triggers.end())
	{
		QuinceTriggers::Instance().unload_trigger(trig_iter -> second);
		zone = trig_iter -> second -> getZone();
		triggers.erase(trigger_id);
	}

	ref_counts.erase(ref_iter);
	Integer_Map::iterator int_iter = node_ids.find(trigger_id);
	if (int_iter != node_ids.end())
	{
		node_ids.erase(int_iter);
	}

	int_iter = sequence.find(trigger_id);
	if (int_iter != node_ids.end())
	{
		sequence.erase(int_iter);
	}
	
	if (zone == -1)
	{
		Log(Logs::General, Logs::Quests, "unload: unable to determine zone");
	}

	pair<Integer_Multimap::iterator, Integer_Multimap::iterator> trig_range =
		zone_map.equal_range(zone);

	Integer_Multimap::iterator iter;
	for (iter = trig_range.first; iter != trig_range.second; ++iter)
	{
		// iter -> first is the zone-id
		// iter -> second is one of the trigger-ids in this zone

		if (iter -> second == zone)
		{
			zone_map.erase(iter);
			return;
		}
	}

	remove_trigger_owner(trigger_id, character_id);

	Log(Logs::General, Logs::Quests, "trigger owner list:");
	for (iter=trigger_owner.begin(); iter != trigger_owner.end(); ++iter)
	{
		int ref_trig = iter -> first;
		int ref_char = iter -> second;

		Log(Logs::General, Logs::Quests, "trigger %d char %d", ref_trig, ref_char);
	}

	// don't call on zone-out
	// remove_active_from_database(trigger_id, character_id);
}

void QuinceTriggerCache::remove_trigger_owner(int trigger_id, int character_id)
{
	pair<Integer_Multimap::iterator, Integer_Multimap::iterator> trig_range =
		trigger_owner.equal_range(trigger_id);

// 	Log(Logs::General, Logs::Quests, "attempt to remove trigger %d ref for char %d", 
// 		trigger_id, character_id);

	Integer_Multimap::iterator iter;
	for (iter = trig_range.first; iter != trig_range.second; ++iter)
	{
		// iter -> first is the trigger-dj
		// iter -> second is one of the character ids

		if (iter -> second == character_id)
		{
		// 	Log(Logs::General, Logs::Quests,"found ref in trigger_owner");
			trigger_owner.erase(iter);
			return;
		}
	}
}

void QuinceTriggerCache::remove_active_from_database(int trigger_id, int char_id)
{
	std::string query = StringFormat("DELETE FROM `quince_active_triggers` WHERE trigger_id=%d "
		"AND character_id=%d", trigger_id, char_id);

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error removing active trigger: %s",
			results.ErrorMessage());
		return;
	}
}

// ==============================================================
// returns the number of triggers loaded
// (this is how we can tell when a node is completed, 0 triggers will load)
// ==============================================================
ID_List QuinceTriggerCache::load_triggers_at_seq(int node, int seq, int character_id)
{
	std::string query = StringFormat("SELECT id FROM `quince_triggers` WHERE node_id=%d AND seq=%d",
		node, seq);

	const char *error_msg = "[QUINCE] error loading seq triggers: %s";

	Log(Logs::General, Logs::Quests,"loading triggers for node %d seq %d",
		node, seq);

	ID_List trigger_ids;

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error loading seq triggers: %s",
			results.ErrorMessage());
		return trigger_ids;
	}

	for (auto row = results.begin(); row != results.end(); ++row)
	{
		int trigger_id = atoi(row[0]);
		load_trigger(trigger_id, seq, character_id);
		trigger_ids.push_back(trigger_id);

	}

	Log(Logs::General, Logs::Quests, "found %d triggers", trigger_ids.size());
	return trigger_ids;
}

void QuinceTriggerCache::process(int trigger_id, Event *event)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		Log(Logs::General, Logs::Quests,"trigger %d not in cache", trigger_id);
		return;
	}

	QuinceTrigger *trigger = iter -> second;
	trigger -> process(event);
}

// ==============================================================
// Return a list of characters associated with a trigger
// ==============================================================
ID_List QuinceTriggerCache::associated_clients(int trigger_id)
{
	ID_List client_ids;

	pair<Integer_Multimap::iterator, Integer_Multimap::iterator> client_range =
		trigger_owner.equal_range(trigger_id);
	Integer_Multimap::iterator iter;

	for (iter = client_range.first; iter != client_range.second; ++iter)
	{
		client_ids.push_back(iter -> second);
	}

	return client_ids;
}

// ==============================================================
// load_activation_triggers:  load permanent triggers used to gain quests
//
// As an example, the initial "hail" of an NPC starts an exchange
// which may lead to the quest being accepted by the character.  These
// initial triggers are always available to all characters (unlike most
// triggers which are restricted to characters having the quest).
//
// These "activation" triggers have sequence #0 in the database.
//
// Activation triggers are loaded on zone boot, and unloaded when the
// zone shuts down.
//
// Some assumptions:  only match-triggers will be used here
// ==============================================================
void QuinceTriggerCache::load_activation_triggers(int zone)
{
	std::string query = StringFormat(
		"SELECT id, quest_id FROM `quince_triggers` WHERE zone=%d "
		"AND seq=0", zone);

	Log(Logs::General, Logs::Quests,"loading activation trigs for zone %d",
		zone);

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error loading activation triggers: %s",
			results.ErrorMessage());
		return;
	}

	for (auto row = results.begin(); row != results.end(); ++row)
	{
		int trigger_id = atoi(row[0]);
		int quest_id = atoi(row[1]);

		// permanent triggers stored in vector until zone shutdown
		// trigger-ids are server-unique
		QuinceTrigger *trigger = QuinceTriggers::Instance().load_trigger(trigger_id);
		if (trigger != NULL)
		{
			activation_triggers[zone].push_back(trigger);
			activation_ids.push_back(trigger_id);
			activated_quest[trigger_id] = quest_id;

			QuestEventID type = trigger -> getType();
			triggers[trigger_id] = trigger;

			if (type == EVENT_PROXIMITY)
			{
				Log(Logs::Moderate, Logs::Quests,"pushing polled act trigger %d",
 					trigger_id);
				polled_triggers.push_back(trigger_id);
			}
		}
	}
}

// ==============================================================
// Return the type for a trigger.  Mainly used so that subquest triggers
// can be treated specially.
//
// Subquest triggers fire differently, and do not appear in activity lists.
// ==============================================================
bool QuinceTriggerCache::getType(int trigger_id, QuestEventID& type)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	type =  iter->second -> getType();
	return true;
}

// ==============================================================
// Return the subordinate node for a subquest trigger
// ==============================================================
bool QuinceTriggerCache::get_sub_node(int trigger_id, int& sub_node)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	QuestEventID type =  iter -> second -> getType();
	if (type != EVENT_SUBQUEST)
	{
		return false;
	}

	QSubquest *t = (QSubquest *) iter -> second;
	sub_node =  t -> get_sub_node();
	Log(Logs::General, Logs::Quests, "get_sub_node sees trigger %d with subnode %d",
			trigger_id, sub_node);
	return true;
}

// ==============================================================
// Return a printable description of a trigger's task
//
// This is sent to the client quest journal
// ==============================================================
bool QuinceTriggerCache::get_task(int trigger_id, string& task)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	QuinceTrigger *t = iter -> second;
	task = t -> getTask();
	return true;
}

// ==============================================================
// Return the zone(s) associated with a trigger
//
// 0 = all zones, -1 = unknown zone
// ==============================================================
bool QuinceTriggerCache::get_zone(int trigger_id, int& zone)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	QuinceTrigger *t = iter -> second;
	zone = t -> getZone();
	return true;
}

// ==============================================================
// Return the sequence number for a trigger.  All triggers with the same
// sequence will associate -- giving the player a choice of which activity
// to complete
// ==============================================================
bool QuinceTriggerCache::get_seq(int trigger_id, int& seq)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	Integer_Map::const_iterator seq_iter = sequence.find(trigger_id);
	if (seq_iter == sequence.end())
	{
		return false;
	}

	seq = seq_iter -> second;
	return true;
}

bool QuinceTriggerCache::get_count(int trigger_id, int& count)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	count = iter -> second -> get_count();

	return true;
}

bool QuinceTriggerCache::get_subroutine(int trigger_id, string& name)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	name = iter->second->getSubroutine();
	return true;
}

// ==============================================================
// Return the quest-id for a trigger.  Not currently used.  The NodeCache
// is typically where this info is obtained
// ==============================================================
bool QuinceTriggerCache::quest_for(int trigger_id, int& quest_id)
{
	// first handle activation triggers -- not regular cache
	Integer_Map::const_iterator iter = activated_quest.find(trigger_id);
	if (iter != activated_quest.end())
	{
		quest_id = iter -> second;
		return true;
	}

	// now check the node cache
	int node_id;
	if (! node_for(trigger_id, node_id))
	{
		Log(Logs::General, Logs::Quests, "TC: no node for trigger %d",
			trigger_id);
		return false;
	}

	int qid;
	if (! QNodeCache::Instance().quest_for(node_id, qid))
	{
		Log(Logs::General, Logs::Quests, "TC: no quest for node %d",
			node_id);
		return false;
	}

	quest_id = qid;
	return true;
}

// ==============================================================
// determine if a trigger-id belongs to an activation trigger
// ==============================================================
bool QuinceTriggerCache::is_activation_trigger(int trigger_id)
{
	bool found = (std::find(activation_ids.begin(), activation_ids.end(), trigger_id)
		!= activation_ids.end());

	return found;
}

bool QuinceTriggerCache::is_repeatable_trigger(int trigger_id)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	// non-existent trigger not required
	if (iter == triggers.end())
	{
		Log(Logs::General, Logs::Quests, "irt: no trigger %d", trigger_id);
		return false;
	}

	return (iter -> second -> isRepeatable());
}

bool QuinceTriggerCache::is_boolean(int trigger_id)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		Log(Logs::General, Logs::Quests,  "ib: no trigger %d", trigger_id);
		return false;
	}

	return (iter -> second -> is_boolean());
}

bool QuinceTriggerCache::init_sat_for_trigger(int trigger_id)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		Log(Logs::General, Logs::Quests,  "isat: no trigger %d", trigger_id);
		return true;
	}

	return (iter -> second -> initSatisfied());
}

// ==============================================================
// display -- dump the contents of the cache for debugging
// ==============================================================
void QuinceTriggerCache::display(Client *client) const
{
	stringstream ss;

	client -> Message(0, "Trigger Cache:");

#if LOG_CACHE
	Log(Logs::Detail, Logs::Quests,"Trigger Cache:");  
#endif

	Trigger_Map::const_iterator iter;
	for (iter = triggers.begin(); iter != triggers.end(); ++iter)
	{
		int trigger_id = iter -> first;
		const QuinceTrigger *t = iter -> second;

		Integer_Map::const_iterator int_iter;

		int node_id = -1;
		int_iter = node_ids.find(trigger_id);
		if (int_iter != node_ids.end())
		{
			node_id = int_iter -> second;
		}

		int seq = -1;
		int_iter = sequence.find(trigger_id);
		if (int_iter != sequence.end())
		{
			seq = int_iter -> second;
		}

		int refs = -1;
		int_iter = ref_counts.find(trigger_id);
		if (int_iter != ref_counts.end())
		{
			refs = int_iter -> second;
		}

		ss << "trigger " << std::setw(18) <<  t -> getName() << " (" << trigger_id << ") -- " <<
			"node " << node_id << ":" << seq << ", " << refs << " refs";

#if LOG_CACHE
		Log(Logs::Detail, Logs::Quests,ss.str().c_str());
#endif
		client -> Message(0, ss.str().c_str());
		ss.str("");

	}
}

void QuinceTriggerCache::process_global_activation(Event *event)
{
	QuestEventID event_type = event -> GetType();

	Zone_Trigger_Map::const_iterator z_iter = activation_triggers.begin();

	while (z_iter != activation_triggers.end())
	{
		Trigger_List::const_iterator t_iter = z_iter -> second.begin();

		while (t_iter != z_iter -> second.end())
		{
			QuinceTrigger *trigger = *t_iter;
			if (event_type == trigger -> getType())
			{
				trigger -> process(event);
			}
			++t_iter;
		}

		++z_iter;
	}
}

// ==============================================================
// Quest_Nodes represent one step/goal for a quest.  A node contains a set
// of triggers arranged by sequence (0 = activation, 1+ are displayed in journal)
// ==============================================================

Quest_Node::Quest_Node(int qid, int nid)
{
	quest_id = qid;
	node_id = nid;

	std::string Trigger_Query = StringFormat(
		"SELECT id, seq FROM `quince_triggers` "
		"WHERE quest_id=%d AND node_id=%d", quest_id, node_id);

	const char *error_msg = "[QUINCE] error loading trigger ids: %s";

	std::string Node_Query = StringFormat(
		"SELECT name, task, parent_trigger FROM `quince_questnodes` "
		"WHERE quest_id=%d AND id=%d", quest_id, node_id);

	const char *node_err = "[QUINCE] error loading node %d data: %s";

	ref_count = 1;
	parent_trigger = -1;		// by default, no parent

	int trigger_count = 0;		// number of triggers loaded

	auto results = database.QueryDatabase(Trigger_Query);
	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error loading trigger ids: %s",
			results.ErrorMessage());
	}
	else
	{
		trigger_count = results.RowCount();

		for (auto row = results.begin(); row != results.end(); ++row)
		{
			int trigger_id = atoi(row[0]);
			int seq = atoi(row[1]);

			Trigger_Binding bind = {trigger_id, seq};
			trigger_ids.push_back(bind);

			Log(Logs::General, Logs::Quests, "trigger %d seq %d",
				trigger_id, seq);
		}

		// sort the trigger list
		std::sort(trigger_ids.begin(), trigger_ids.end(), Binding_Sorter());
	}


	Log(Logs::General, Logs::Quests, "%d trigger ids for quest %d  node %d loaded",
		trigger_count, quest_id, node_id);


	Log(Logs::General, Logs::Quests, "Node query:  %s", Node_Query.c_str());

	results = database.QueryDatabase(Node_Query);

	if (! results.Success())
	{
		Log(Logs::General, Logs::Quests, "Error loading data for node %d:  %s",
			results.ErrorMessage());

	}
	else
	{
		Log(Logs::General, Logs::Quests, "node query success, %d rows returned", results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row)
		{
			name = row[0];
			task = row[1];

			if (row[2] != NULL)
			{
				parent_trigger = atoi(row[2]);
				Log(Logs::General, Logs::Quests, "parent trigger: %d", parent_trigger);
			}
			else
			{
				Log(Logs::General, Logs::Quests, "no parent trigger");
			}

			Log(Logs::General, Logs::Quests, "node %d (%s) loaded", node_id, name.c_str());
		}
	}
}

// ==============================================================
// Quest_Nodes are stored in the Node Cache, and are reference counted.
// The reference counts are kept with the node, rather than in the cache.
// ==============================================================
void Quest_Node::add_reference()
{
	ref_count++;
}

int Quest_Node::remove_reference()
{
	if (ref_count > 0)
	{
		ref_count--;
	}

	return ref_count;
}

// ==============================================================
// Return the set of triggers at a particular sequence number
// ==============================================================
ID_List Quest_Node::trigger_ids_at_seq(int seq)
{
	ID_List ids;

	for (unsigned int i = 0; i < trigger_ids.size(); i++)
	{
		if (trigger_ids[i].seq == seq)
		{
			ids.push_back(trigger_ids[i].trigger_id);
		}
	}

	return ids;
}


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
				parse -> EventCommon(EVENT_NULL, 0, nullptr, client->CastToNPC(), nullptr, client, 0, false,
					nullptr, quest_id, subroutine);


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

bool Entity_Registration::present(QuestEventID type, int entity)
{
	list<Event_Registration>::const_iterator iter;

	for (iter = entries.begin(); iter != entries.end(); ++iter)
	{
		if ((iter -> type == type) && (iter -> entity == entity))
		{
			return true;
		}
	}

	return false;
}

ID_List Entity_Registration::clients_for(QuestEventID type, int entity)
{
	ID_List clients;
	list<Event_Registration>::const_iterator iter;

	for (iter = entries.begin(); iter != entries.end(); ++iter)
	{
		if ((iter -> type == type) && (iter -> entity == entity))
		{
			clients.push_back(iter -> char_id);
		}
	}

	return clients;
}

void Entity_Registration::insert(QuestEventID type, int entity, int char_id)
{
	Event_Registration reg;
	reg.type = type;
	reg.entity = entity;
	reg.char_id = char_id;

	entries.push_back(reg);
}

// assume that clients are only entered one time
void Entity_Registration::remove(QuestEventID type, int entity, int char_id)
{
	list<Event_Registration>::iterator iter;

	for (iter = entries.begin(); iter != entries.end(); ++iter)
	{
		if ((iter -> type == type) && (iter -> entity == entity) && 
			(iter -> char_id == char_id))
		{
			entries.erase(iter);
			return;
		}
	}
}

// remove all with a particular character-id
void Entity_Registration::remove_all(int char_id)
{
	list<Event_Registration>::iterator iter;

	for (iter = entries.begin(); iter != entries.end(); ++iter)
	{
		if (iter -> char_id == char_id)
		{
			entries.erase(iter++);
		}
		else
		{
			++iter;
		}
	}
}

void Entity_Set::insert(int char_id, int eset_id, EntityType type, int entity_id)
{
	ESet_Record record;
	record.char_id = char_id;
	record.eset_id = eset_id;
	record.entity_id = entity_id;
	record.type = type;

	Log(Logs::General, Logs::Quests, "eset %d adds id %d", eset_id, entity_id);

	entities[char_id].push_back(record);
	eset_to_char[eset_id] = char_id;
}

void Entity_Set::insert(int eset_id, EntityType type, int entity_id)
{
	map<int,int>::const_iterator iter = eset_to_char.find(eset_id);
	int char_id;
	if (iter == eset_to_char.end())
	{
		// cannot add to non-existent eset
		return;
	}
	else
	{
		char_id = iter->second;
	}

	insert(char_id, eset_id, type, entity_id);
}

void Entity_Set::remove(int char_id, int eset_id, int entity_id)
{
	list<ESet_Record>::iterator iter;
	for (iter = entities[char_id].begin(); iter != entities[char_id].end();
		++iter)
	{
		if ((iter -> eset_id == eset_id) &&
			(iter -> entity_id == entity_id))
		{
			entities[char_id].erase(iter);
			return;
		}
	}
}

bool Entity_Set::is_member(int char_id, int eset_id, int entity_id)
{
	map<int, ESet_List>::iterator iter = entities.find(char_id);
	if (iter == entities.end())
	{
		Log(Logs::General, Logs::Quests, "is_member:  eset %d NOT found", eset_id);
		return false;
	}
	
	// eset exists
	if (iter != entities.end())
	{
		Log(Logs::General, Logs::Quests, "is_member:  eset %d found", eset_id);

		ESet_List::iterator eset_iter;

		for (eset_iter = iter -> second.begin();
			 eset_iter != iter -> second.end(); ++eset_iter)
		{
			Log(Logs::General, Logs::Quests, "is_member:  consider id %d",
				eset_iter -> entity_id);

			if (eset_iter -> entity_id == entity_id)
			{
				Log(Logs::General, Logs::Quests, "is_member:  YES");

				return true;
			}
		}
	}
	else
	{
		Log(Logs::General, Logs::Quests, "is_member:  eset %d NOT found", eset_id);
	}

	Log(Logs::General, Logs::Quests, "is_member:  NO");

	return false;
}


ID_List Entity_Set::esets_containing(int entity_id, int char_id)
{
	ID_List members;

	Log(Logs::Detail, Logs::Quests,  "esets_containing:  id %d", entity_id);

	// map of char_id to ESet_List
	map<int, ESet_List>::iterator iter = entities.find(char_id);
	ESet_List::iterator eset_iter;

	if (iter == entities.end())
	{
		Log(Logs::General, Logs::Quests, "no esets for character %d", char_id);
		return members;
	}

	// look at all ESet_Records associated with the client (1 per npc)
	for (eset_iter = iter -> second.begin(); eset_iter != iter -> second.end(); 
		++eset_iter)
	{
		Log(Logs::Detail, Logs::Quests, "consider eset %d", 
			eset_iter -> eset_id);

		if (eset_iter -> entity_id == entity_id)
		{
			Log(Logs::Detail, Logs::Quests, "add eset %d to result set", 
				eset_iter -> eset_id);
			members.push_back(eset_iter -> eset_id);
		}
	}

	Log(Logs::General, Logs::Quests, "esets_containing:  found %d sets",
		members.size());
	return members;
}

void Entity_Set::aggro_all(int char_id, int eset_id, int entity_id)
{
	list<ESet_Record>::iterator iter;
	Mob *target = NULL;

	NPC *npc = entity_list.GetNPCByID(entity_id);
	Client *client = entity_list.GetClientByID(entity_id);
	if (npc)
	{
		target = npc;
	}
	else if (client)
	{
		target = client;
	}
	else
	{
		Log(Logs::General, Logs::Quests, "aggro_all: no target mob %d", entity_id);
		return;
	}

	map<int, ESet_List>::iterator eset_iter = entities.find(char_id);
	if (eset_iter == entities.end())
	{
		// no esets for this client
		Log(Logs::General, Logs::Quests, "aggro_all: no eset %d", eset_id);
		return;
	}

	Log(Logs::General, Logs::Quests, "attempt to aggro eset %d on entity %d",
		eset_id, entity_id);

	for (iter = entities[char_id].begin(); iter != entities[char_id].end(); ++iter)
	{
		if (iter -> eset_id != eset_id)
		{
			continue;
		}

		// only aggro npcs
		if (iter -> type != Entity_NPC)
		{
			continue;
		}

		NPC *npc = entity_list.GetNPCByID(iter -> entity_id);
		if (npc)
		{
			npc -> AddToHateList(target, 1);
		}
		else
		{
			Log(Logs::General, Logs::Quests, "eset member %d not found",
				iter -> entity_id);
		}

		Log(Logs::General, Logs::Quests, "aggro entity %d on %s",
			iter -> entity_id, target -> GetName());
	}
}

void Entity_Set::assign_faction(int char_id, int eset_id, int faction_id)
{
	list<ESet_Record>::iterator iter;
	Mob *target = NULL;

	map<int, ESet_List>::iterator eset_iter = entities.find(char_id);
	if (eset_iter == entities.end())
	{
		// no esets for this client
		Log(Logs::General, Logs::Quests, "assign_faction: no eset %d", eset_id);
		return;
	}

	Log(Logs::General, Logs::Quests, "attempt to assign faction %d to eset %d",
		faction_id, eset_id);

	for (iter = entities[char_id].begin(); iter != entities[char_id].end(); ++iter)
	{
		if (iter -> eset_id != eset_id)
		{
			continue;
		}

		// only aggro npcs
		if (iter -> type != Entity_NPC)
		{
			continue;
		}

		NPC *npc = entity_list.GetNPCByID(iter -> entity_id);
		if (npc)
		{
		//	npc -> SetPrimaryFaction(faction_id);
			npc -> SetNPCFactionID(faction_id);
		}
		else
		{
			Log(Logs::General, Logs::Quests, "eset member %d not found",
				iter -> entity_id);
		}

		Log(Logs::General, Logs::Quests, "assign faction %d to entity %d",
			faction_id, iter -> entity_id);
	}
}

void Entity_Set::depop_all(int char_id, int eset_id)
{
	Log(Logs::General, Logs::Quests, "depop eset %d", eset_id);

	list<ESet_Record>::iterator iter;
	for (iter = entities[char_id].begin(); iter != entities[char_id].end();)
	{
		NPC *npc;
		Object *obj;
		Doors *door;

		if (iter -> eset_id != eset_id)
		{
			++iter;
			continue;
		}

		switch(iter -> type)
		{
			case Entity_NPC:
				npc = entity_list.GetNPCByID(iter -> entity_id);
				if (npc)
				{
					npc -> Depop();
					Log(Logs::General, Logs::Quests, "depop npc %d",
						iter -> entity_id);
				}
				else
				{
					Log(Logs::General, Logs::Quests, "no npc %d",
						iter -> entity_id);
				}

				iter = entities[char_id].erase(iter);
				break;
			case Entity_Item:
				obj = entity_list.GetObjectByID(iter -> entity_id);
				if (obj)
				{
					obj -> Depop();
					Log(Logs::General, Logs::Quests, "depop item %d",
						iter -> entity_id);
				}
				else
				{
					Log(Logs::General, Logs::Quests, "no item %d",
						iter -> entity_id);
				}
				iter = entities[char_id].erase(iter);
				break;
			case Entity_Door:
				door = entity_list.GetDoorsByID(iter -> entity_id);
				if (door)
				{
					door -> Depop();
					Log(Logs::General, Logs::Quests, "depop door %d",
						iter -> entity_id);
				}
				else
				{
					Log(Logs::General, Logs::Quests, "no door %d",
						iter -> entity_id);
				}
				iter = entities[char_id].erase(iter);
				break;
			case Entity_NPC_Type_ID:
				++iter;
				break;
			default:
				Log(Logs::General, Logs::Quests, "cannot depop type %d",
					iter -> type);
				++iter;
				break;
		}
	}
}

float Entity_Set::min_distance_to_point(int char_id, int eset_id,
	float x, float y, float z)
{
	list<ESet_Record>::iterator iter;
	float min_distance = -1;

	for (iter = entities[char_id].begin(); iter != entities[char_id].end(); ++iter)
	{
		if (iter -> eset_id != eset_id)
		{
			continue;
		}

		NPC *npc = entity_list.GetNPCByID(iter -> entity_id);
		if (npc)
		{
			float dx = npc->GetX() - x;
			float dy = npc->GetY() - y;
			float dz = npc->GetZ() - z;

			float d = (dx*dx) + (dy*dy) + (dz*dz);

			if (min_distance == -1)
			{
				min_distance = d;
			}
			else if (d < min_distance)
			{
				min_distance = d;
			}
		}
	}

	return min_distance;
}

void Entity_Set::arrange_eset(int char_id, int eset_id, float range, float x,
	float y, float z)
{
	list<ESet_Record>::iterator iter;

	map<int, ESet_List>::iterator eset_iter = entities.find(char_id);
	if (eset_iter == entities.end())
	{
		// no esets for this client
		Log(Logs::General, Logs::Quests, "arrange: no eset %d", eset_id);
		return;
	}

	Log(Logs::General, Logs::Quests, "attempt to arrange eset %d", eset_id);

	int eset_size = 0;
	for (iter = entities[char_id].begin(); iter != entities[char_id].end(); ++iter)
	{
		if (iter -> eset_id != eset_id)
		{
			continue;
		}

		if (iter -> type == Entity_NPC)
		{
			eset_size++;
		}
	}

	if (eset_size < 2)
	{
		return;
	}

	float pi = 3.14159;
	float delta_theta = (pi / (2*(eset_size - 1)));

	Log(Logs::General, Logs::Quests, "%d npcs in this eset, r=%f, dt = %f", eset_size,
		range, delta_theta);
	
	int position = 0;
	for (iter = entities[char_id].begin(); iter != entities[char_id].end(); ++iter)
	{

		if (iter -> eset_id != eset_id)
		{
			continue;
		}

		if (iter -> type != Entity_NPC)
		{
			continue;
		}

		NPC *npc = entity_list.GetNPCByID(iter -> entity_id);
		if (npc)
		{
			float dx = range * ((float) cos(position*delta_theta));
			float dy = range * ((float) sin(position*delta_theta));

			Log(Logs::General, Logs::Quests, "dx=%f, dy=%f, arc=%f",
				dx, dy, (position*delta_theta));

			glm::vec3 p;
			p.x = x + dx;
			p.y = y + dy;
			p.z = z;

			float z = zone -> zonemap -> FindBestZ(p, nullptr);
			npc -> SendPosition();
			npc -> GMMove(x + dx, y + dy, z, npc -> GetHeading());
			npc -> SaveGuardSpot(true);

			Log(Logs::General, Logs::Quests, "move npc %d to %f,%f,%f",
				position, x + dx, y + dy, z);
			position++;
		}
		else
		{
			Log(Logs::General, Logs::Quests, "arrange: eset member %d not found",
				iter -> entity_id);
		}
	}
}

bool Entity_Set::exists(int eset_id)
{
	map<int, ESet_List>::iterator iter;
	for (iter = entities.begin(); iter != entities.end(); ++iter)
	{
		ESet_List::const_iterator list_iter;
		for (list_iter = iter -> second.begin(); list_iter != iter -> second.end();
			++list_iter)
		{
			if (list_iter -> eset_id == eset_id)
			{
				return true;
			}
		}
	}

	return false;
}

// ==============================================================
// assign_char -- bind an eset to a character
// Note that the eset may not exist yet
// ==============================================================
void Entity_Set::assign_char(int eset_id, int char_id)
{
	eset_to_char[eset_id] = char_id;
}

// ==============================================================
// QModifications:  manage client-specific modifications and esets
// ==============================================================

QModifications::QModifications()
{
	eset = new Entity_Set;
}

unique_ptr<QModifications> QModifications::_instance;

QModifications&
QModifications::Instance()
{
    if (_instance.get() == NULL)
    {
        _instance.reset (new QModifications);
    }

    return *_instance;
}

// ==============================================================
// Any member of Eset (when associated with char) will have a chance to have
// item_1 exchanged with item_2 upon death.
//
// Association includes group/raid membership.
//
// map key is item_1 (item type, NOT EID)
// ==============================================================
void QModifications::insert_loot_mod(int char_id, int quest_id, int eset_id, int item_1,
		int item_2, int chance)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.eset_id = eset_id;
	mod.item_1_type = item_1;
	mod.item_2_type = item_2;
	mod.chance = chance;

	mod.entity_type = Entity_Item;
	mod.mod_type = Modification_Loot;

	mod_records.insert(pair<int, Selective_Modification> (item_1, mod));
}

// ==============================================================
// give each client a chance to cause a loot exchange
//
// A client may have multiple loot mod records (unlikely), but allow each
// record to roll for a chance to exchange.  Only one exchange is permitted
// for a client.  Only one item is converted per exchange.
//
// A raid mob might drop several candidate items (item_1_type), and there
// may be more than one client running the quest.  Each of these clients
// will therefore roll to convert an item.  So it is possible for multiple
// items to be converted after a single kill.  Let the players decide how
// to allocate the new item_2_type items.
// ==============================================================

void QModifications::perform_loot_mods(NPC *npc, int char_id)
{
	EQEmu::Random random;

#if 0
	Log(Logs::General, Logs::Quests, "perform loot mod req: npc %d, char %d",
		npc -> GetNPCTypeID(), char_id);
#endif
	// all esets containing the dead npc
	ID_List esets = esets_containing(npc -> GetNPCTypeID(), char_id);
	ID_List::iterator eset_iter;

	// the set of modification records applying to the dead npc
	pair<Mod_Multimap::iterator, Mod_Multimap::iterator> mod_set;
	mod_set = mod_records.equal_range(npc -> GetNPCTypeID());

	for (eset_iter = esets.begin(); eset_iter != esets.end(); ++eset_iter)
	{
		Log(Logs::Detail, Logs::Quests, "consider eset %d", *eset_iter);

		// need to check all mod records (don't know item to swap)
		Mod_Multimap::iterator mod_iter;
		for (mod_iter = mod_records.begin(); mod_iter != mod_records.end(); ++mod_iter)
		{
			Log(Logs::General, Logs::Quests, "consider mod record for item %d",
				mod_iter -> first);

			if (mod_iter -> second.item_1_type)
			{
				// make sure the npc has the item to exchange
				if (! npc -> HasItem(mod_iter -> second.item_1_type))
				{
					Log(Logs::General, Logs::Quests, "src item not in inventory");
					continue;
				}
			}

			// does this eset match the modification record?
			if (mod_iter -> second.eset_id == *eset_iter)
			{
				Log(Logs::General, Logs::Quests, "rolling for exchange");

				// a chance to exchange the item
				if (random.Int(0, 99) <= mod_iter -> second.chance)
				{
					Log(Logs::General, Logs::Quests, "swap!");
					if (mod_iter -> second.item_1_type)
					{
						npc -> RemoveItem(mod_iter -> second.item_1_type, 1, 0);
					}
					if (mod_iter -> second.item_2_type)
					{
						npc -> AddItem(mod_iter -> second.item_2_type, 1, false);
					}
					return;
				}
			}
		}
	}
}

// ==============================================================
// Add an item to a vendor's inventory (for associated clients)
// ==============================================================
void QModifications::add_vendor_item(int vendor_id, int item_id, int price, 
		int quest_id, int char_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.entity_id = vendor_id;
	mod.entity_type = Entity_NPC;
	mod.item_1_type = item_id;
	mod.index = price;

	mod.mod_type = Modification_Vendor;
}

// ==============================================================
// An item of type item_type may only be used on members of an ESet
// (Assume that these quest items will be no-drop)
//
// map key is the item_type
// ==============================================================
void QModifications::insert_item_limit(int char_id, int quest_id, int item_type, 
	int eset_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.eset_id = eset_id;
	mod.item_1_type = item_type;

	mod.entity_type = Entity_Item;
	mod.mod_type = Modification_OnlyUsableOn;

	mod_records.insert(pair<int, Selective_Modification> (item_type, mod));
}

void QModifications::insert_item_limit(int char_id, int quest_id, int item_type, 
	float x, float y, float z, float threshold)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.item_1_type = item_type;
	mod.x = x;
	mod.y = y;
	mod.z = z;
	mod.distance = threshold;

	mod.entity_type = Entity_Item;
	mod.mod_type = Modification_OnlyUsableAt;

	mod_records.insert(pair<int, Selective_Modification> (item_type, mod));
}


// ==============================================================
// See if a character may use an item on an entity
// ==============================================================
bool QModifications::is_usage_permitted(int char_id, int item_id, int entity_id)
{
	Log(Logs::General, Logs::Quests, "is_usage_permitted:  char %d, item %d, eid %d",
			char_id, item_id, entity_id);

	pair<Mod_Multimap::iterator, Mod_Multimap::iterator> mod_set;
	mod_set = mod_records.equal_range(item_id);

	if (mod_set.first == mod_records.end())
	{
		Log(Logs::General, Logs::Quests, "no modification record for item %d",
			item_id);
		return false;
	}

	Client *c = Quince::Instance().get_client(char_id);
	if (c == NULL)
	{
		Log(Logs::General, Logs::Quests, "no client %d", char_id);
		return false;
	}

	// look at each modification for this item type
	Mod_Multimap::iterator iter;
	for (iter = mod_set.first; iter != mod_set.second; ++iter)
	{
		Log(Logs::General, Logs::Quests, "consider eset %d", 
			iter -> second.eset_id);

		if ((iter -> second.char_id == char_id) && 
			(iter -> second.item_1_type == item_id) &&
			(eset -> is_member(char_id, iter -> second.eset_id, entity_id)) &&
			(iter -> second.mod_type == Modification_OnlyUsableOn))
		{
			Log(Logs::General, Logs::Quests, "item may be used on target");
			return true;
		}

		float t_2 = iter -> second.distance * iter -> second.distance;
		float dx = iter -> second.x - c -> GetX();
		float dy = iter -> second.y - c -> GetY();
		float dz = iter -> second.z - c -> GetZ();

		float dist = (dx*dx) + (dy*dy) + (dz*dz);

		if ((iter -> second.char_id == char_id) &&
			(iter -> second.item_1_type == item_id) &&
			(dist < t_2))
		{
			Log(Logs::General, Logs::Quests, "item may be used here");
			return true;
		}
		else
		{
			Log(Logs::General, Logs::Quests, "d2 = %f, t2 = %f",
				dist, t_2);
		}

	}

	Log(Logs::General, Logs::Quests, "item may not be used here");
	return false;
}

// ==============================================================
// A particular Mob will only be seen by a client or those associated
//
// map key is the npc_id to restrict
// ==============================================================

void QModifications::limit_npc_visibility(int char_id, int quest_id, int npc_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.entity_id = npc_id;
	mod.entity_type = Entity_NPC; 
	mod.mod_type = Modification_NPC_Visibility;

	mod_records.insert(pair<int, Selective_Modification> (npc_id, mod));

	NPC *npc = entity_list.GetNPCByID(npc_id);
	process_new_npc(npc);
}

// ==============================================================
// Mobs in a particular spawngroup will only be seen by a client or those associated
//
// map key is the spawngroup to restrict
// ==============================================================

void QModifications::limit_spawngroup_visibility(int char_id, int quest_id, 
		int spawngroup_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.entity_id = spawngroup_id;
	mod.entity_type = Entity_Spawngroup; 
	mod.mod_type = Modification_Spawngroup_Visibility;

	mod_records.insert(pair<int, Selective_Modification> (spawngroup_id, mod));
	hide_spawngroup_from_client(char_id, spawngroup_id);
}


// ==============================================================
// A particular Player will only be seen by a client when within dist and assoc
// ==============================================================
void QModifications::limit_player_visibility(int char_id, int quest_id, int client_id, 
	float distance)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.entity_id = client_id;
	mod.distance = distance;

	mod.entity_type = Entity_Player; 
	mod.mod_type = Modification_Player_Visibility;

	mod_records.insert(pair<int, Selective_Modification> (client_id, mod));
}

// ==============================================================
// A particular Group will only be seen by a client when within dist and assoc
// ==============================================================
void QModifications::limit_group_visibility(int char_id, int quest_id, int group_id, 
	float distance)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.entity_id = group_id;
	mod.distance = distance;

	mod.entity_type = Entity_Group; 
	mod.mod_type = Modification_Group_Visibility;

	mod_records.insert(pair<int, Selective_Modification> (group_id, mod));
}

// ==============================================================
// A particular Raid will only be seen by a client when within dist and assoc
// ==============================================================
void QModifications::limit_raid_visibility(int char_id, int quest_id, int raid_id, 
	float distance)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.entity_id = raid_id;
	mod.distance = distance;

	mod.entity_type = Entity_Raid; 
	mod.mod_type = Modification_Raid_Visibility;

	mod_records.insert(pair<int, Selective_Modification> (raid_id, mod));
}

void QModifications::hide_entity_from_client(int char_id, int entity_id)
{
	Client *client = entity_list.GetClientByCharID(char_id);

	Log(Logs::General, Logs::Quests, "hiding entity %d", entity_id);

	EQApplicationPacket* app = new EQApplicationPacket(OP_ClientUpdate, sizeof(PlayerPositionUpdateServer_Struct));
	PlayerPositionUpdateServer_Struct* spu = (PlayerPositionUpdateServer_Struct*)app->pBuffer;
	memset(spu, 0xff, sizeof(PlayerPositionUpdateServer_Struct));
	spu->spawn_id = entity_id;
	spu->x_pos = FloatToEQ19(0);
	spu->y_pos = FloatToEQ19(0);
	spu->z_pos = FloatToEQ19(-2000);
	spu->delta_x = FloatToEQ13(0);
	spu->delta_y = FloatToEQ13(0);
	spu->delta_z = FloatToEQ13(0);
	spu->heading = FloatToEQ12(0);
	spu->animation = 0;
	spu->delta_heading = FloatToEQ10(0);
	spu -> padding0002 = 0;
	spu -> padding0006 = 7;
	spu -> padding0014 = 0x7f;
	spu -> padding0018 = 0x5df27;

	client->QueuePacket(app);
	safe_delete(app);
}

void QModifications::hide_spawngroup_from_client(int char_id, int sg_id)
{
	list<NPC*> npc_list;
	entity_list.GetNPCList(npc_list);
	list<NPC*>::iterator iter;

	for (iter = npc_list.begin(); iter != npc_list.end(); ++iter)
	{
		NPC *current = *iter;
		if (! current)
		{
			continue;
		}

		if (current -> GetSp2() == sg_id)
		{
			process_new_npc(current);
		}
	}
}

void QModifications::show_entity_to_client(int char_id, int entity_id)
{
	Client *client = entity_list.GetClientByCharID(char_id);

	Log(Logs::General, Logs::Quests, "showing entity %d", entity_id);
	EQApplicationPacket* app = new EQApplicationPacket(OP_ClientUpdate, sizeof(PlayerPositionUpdateServer_Struct));
	PlayerPositionUpdateServer_Struct* spu = (PlayerPositionUpdateServer_Struct*)app->pBuffer;
	Mob *mob = entity_list.GetMob(entity_id);
	mob -> MakeSpawnUpdateNoDelta(spu);
	client->QueuePacket(app);
	safe_delete(app);
}

// ==============================================================
// A newly spawned NPC might need filtering
// ==============================================================
void QModifications::process_new_npc(NPC *npc)
{
	list<Client*> client_list;
	entity_list.GetClientList(client_list);
	list<Client*>::iterator iter;

	int eid = npc -> GetID();

	for (iter = client_list.begin(); iter != client_list.end(); ++iter)
	{
		Client *c = *iter;
		if (c)
		{
			int char_id = c -> CharacterID();
			if (!is_entity_visible(eid, char_id))
			{
				hide_entity_from_client(char_id, eid);
			}
		}
	}
}

// ==============================================================
// common visibility limitation
// ==============================================================

void QModifications::limit_visibility(int char_id, int quest_id, float distance)
{
	Client *client = entity_list.GetClientByCharID(char_id);
	Group *group = entity_list.GetGroupByClient(client);
	Raid *raid = entity_list.GetRaidByClient(client);

	if (raid)
	{
		limit_raid_visibility(char_id, quest_id, raid -> GetID(), distance);
	}
	else if (group)
	{
		limit_group_visibility(char_id, quest_id, group -> GetID(), distance);
	}
	else if (client)
	{
		limit_player_visibility(char_id, quest_id, char_id, distance);
	}
}

// ==============================================================
// Members of an ESet are immune from interaction with player
//
// map key is the char_id 
// ==============================================================
void QModifications::insert_player_immunity(int char_id, int quest_id, int eset_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.eset_id = eset_id;
	mod.entity_id = char_id;

	mod.entity_type = Entity_Player; 
	mod.mod_type = Modification_Player_Immunity;

	mod_records.insert(pair<int, Selective_Modification> (char_id, mod));
}

// ==============================================================
// Members of an ESet are immune from interaction with group
// ==============================================================
void QModifications::insert_group_immunity(int char_id, int quest_id, int group_id, 
	int eset_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.eset_id = eset_id;
	mod.entity_id = group_id;

	mod.entity_type = Entity_Group; 
	mod.mod_type = Modification_Group_Immunity;

	mod_records.insert(pair<int, Selective_Modification> (group_id, mod));
}

// ==============================================================
// Members of an ESet are immune from interaction with raid
// ==============================================================
void QModifications::insert_raid_immunity(int char_id, int quest_id, int raid_id, 
	int eset_id)
{
	Selective_Modification mod;
	mod.char_id = char_id;
	mod.quest_id = quest_id;
	mod.eset_id = eset_id;
	mod.entity_id = raid_id;

	mod.entity_type = Entity_Raid; 
	mod.mod_type = Modification_Raid_Immunity;

	mod_records.insert(pair<int, Selective_Modification> (raid_id, mod));
}

// ==============================================================
// Common immunity insertion
// ==============================================================
void QModifications::insert_immunity(int char_id, int quest_id, int eset_id)
{
	Client *client = entity_list.GetClientByCharID(char_id);
	Group *group = entity_list.GetGroupByClient(client);
	Raid *raid = entity_list.GetRaidByClient(client);

	if (raid)
	{
		insert_raid_immunity(char_id, quest_id, raid -> GetID(), eset_id);
	}
	else if (group)
	{
		insert_group_immunity(char_id, quest_id, group -> GetID(), eset_id);
	}
	else if (client)
	{
		insert_player_immunity(char_id, quest_id, eset_id);
	}

}

bool QModifications::is_entity_immune(int entity_id, int char_id)
{
	pair<Mod_Multimap::iterator, Mod_Multimap::iterator> mod_set;
	mod_set = mod_records.equal_range(char_id);

#if 0
	Log(Logs::General, Logs::Quests, "is_entity_immune: eid %d, char %d",
		entity_id, char_id);
#endif
	if (mod_set.first -> second.char_id != char_id)
	{
		Log(Logs::Detail, Logs::Quests, "no modification record for this char");
		return false;
	}

	// look at each modification for this char
	Mod_Multimap::iterator iter;
	for (iter = mod_set.first; iter != mod_set.second; ++iter)
	{
		Log(Logs::General, Logs::Quests, "consider mod for char %d", 
			iter -> second.char_id);

		if (! eset -> is_member(char_id, iter -> second.eset_id, entity_id))
		{
			Log(Logs::General, Logs::Quests, "entity not in eset for this record");
		}

		if ((iter -> second.mod_type == Modification_Player_Immunity) ||
			 (iter -> second.mod_type == Modification_Group_Immunity) ||
			 (iter -> second.mod_type == Modification_Raid_Immunity))
		{
			Log(Logs::General, Logs::Quests, "immunity found for this entity");

			return true;
		}
	}

	return false;
}

bool QModifications::is_entity_visible(int entity_id, int char_id)
{
	Client *client = entity_list.GetClientByCharID(char_id);
	Group *group = entity_list.GetGroupByClient(client);
	Raid *raid = entity_list.GetRaidByClient(client);

	// if at least one applicable restriction is found, then only those clients
	// who are associated with the quest may see the entity

	bool restriction_found = false;

	// locate the modification records applying to this entity
	Mod_Multimap::const_iterator iter;
	for (iter = mod_records.begin(); iter != mod_records.end(); ++iter)
	{
		// NPC has a specific restriction applied
		if ((iter -> second.mod_type == Modification_NPC_Visibility) &&
			(iter -> second.entity_id == entity_id))
		{
			restriction_found = true;
			if (is_associated(char_id, iter -> second.char_id))
			{
				return true;
			}
		}

		// NPC is a member of a restricted spawngroup
		if (iter -> second.mod_type == Modification_Spawngroup_Visibility)
		{
			NPC *npc = entity_list.GetNPCByID(entity_id);
			if (npc)
			{
				if (iter -> second.entity_id == npc -> GetSp2())
				{
					// mob is in a restricted spawngroup
					restriction_found = true;

					if (is_associated(char_id, iter -> second.char_id))
					{
						return true;
					}
				}
			}
		}
	}

	if (restriction_found)
	{
		return false;			// modifications found, but none grant access
	}
	else
	{
		return true;			// no modifications apply to this NPC
	}
}

bool QModifications::is_associated(int char_1_id, int char_2_id)
{
	if (char_1_id == char_2_id)
	{
		return true;
	}

	Client *client1 = entity_list.GetClientByCharID(char_1_id);
	Group *group1 = entity_list.GetGroupByClient(client1);
	Raid *raid1 = entity_list.GetRaidByClient(client1);

	Client *client2 = entity_list.GetClientByCharID(char_2_id);
	Group *group2 = entity_list.GetGroupByClient(client2);
	Raid *raid2 = entity_list.GetRaidByClient(client2);

	// in the same group
	if (group1 && (group1 == group2))
	{
		return true;
	}

	// in the same raid
	if (raid1 && (raid1 == raid2))
	{
		return true;
	}

	return false;
}

void QModifications::remove(Modification_Type type, int char_id, int quest_id,
	int entity_id)
{
	pair<Mod_Multimap::iterator, Mod_Multimap::iterator> mod_set;
	mod_set = mod_records.equal_range(entity_id);

	Log(Logs::General, Logs::Quests, "QMods req to remove char %d, quest %d, eid %d",
				char_id, quest_id, entity_id);

	Mod_Multimap::iterator iter;
	for (iter = mod_set.first; iter != mod_set.second; )
	{
		if (iter->second.mod_type == type)
		{
			Log(Logs::General, Logs::Quests, "removing 1 record");

			Mod_Multimap::iterator save_iter = iter;
			save_iter++;
			mod_records.erase(iter);
			iter = save_iter;
		}
		else
		{
			++iter;
		}
	}

	if (type == Modification_NPC_Visibility)
	{
		NPC *npc = entity_list.GetNPCByID(entity_id);
		if (npc)
		{
			npc -> SendPosition();
		}
	}

	Log(Logs::General, Logs::Quests, "QMods all mods removed");
}

// ==============================================================
// Entity_Set accessors
// ==============================================================

void QModifications::add_to_eset(int char_id, int eset_id,
	EntityType type, int entity_id)
{
	eset -> insert(char_id, eset_id, type, entity_id);
	char_to_eset[char_id].insert(eset_id);

	Log(Logs::General, Logs::Quests, "QMods associate char %d with eset %d",
		char_id, eset_id);
}

void QModifications::remove_from_eset(int char_id, int eset_id,
	EntityType type, int entity_id)
{
	eset -> remove(char_id, eset_id, entity_id);
}

void QModifications::depop_eset(int char_id, int eset_id)
{
	eset -> depop_all(char_id, eset_id);
}

void QModifications::aggro_eset(int char_id, int eset_id, int entity_id)
{
	eset -> aggro_all(char_id, eset_id, entity_id);
}

void QModifications::assign_eset_faction(int char_id, int eset_id, int faction_id)
{
	eset -> assign_faction(char_id, eset_id, faction_id);
}

ID_List QModifications::esets_containing(int npc_id, int char_id)
{
	return eset -> esets_containing(npc_id, char_id);
}

float QModifications::min_distance_to_point(int char_id, int eset_id,
	float x, float y, float z)
{
	return eset -> min_distance_to_point(char_id, eset_id, x, y, z);
}

void QModifications::arrange_eset(int char_id, int eset_id, float range,
	float x, float y, float z)
{
	eset -> arrange_eset(char_id, eset_id, range, x, y, z);
}

bool QModifications::eset_exists(int eset_id)
{
	return eset -> exists(eset_id);
}

// ==============================================================
// When a quest is cancelled (or completes) depop any remaining
// entities, and remove the modification records
// ==============================================================
void QModifications::cancel_task(int char_id, int quest_id)
{
	Log(Logs::General, Logs::Quests, "QMods cancel_task for char %d quest %d",
		char_id, quest_id);

	Mod_Multimap::iterator iter;
	for (iter = mod_records.begin(); iter != mod_records.end(); /* no inc */)
	{
		if ((iter -> second.char_id == char_id) &&
			(iter -> second.quest_id == quest_id))
		{
			Mod_Multimap::iterator save_iter = iter;
			save_iter++;
			mod_records.erase(iter);
			iter = save_iter;
		}
		else
		{
			++iter;
		}
	}

	// remove esets associated with this quest
	Integer_MapSet::iterator eset_iter = char_to_eset.find(char_id);
	if (eset_iter != char_to_eset.end())
	{
		set<int>::iterator set_iter;
		for (set_iter = eset_iter -> second.begin(); 
			 set_iter != eset_iter -> second.end(); ++set_iter)
		{
			eset -> depop_all(char_id, *set_iter);
		}

		char_to_eset.erase(eset_iter);
	}
}

void QModifications::unload_char(int char_id)
{
	Log(Logs::General, Logs::Quests, "qmods: unload all for char %d", char_id);

	Mod_Multimap::iterator iter;
	for (iter = mod_records.begin(); iter != mod_records.end(); /* no inc */)
	{
		if (iter -> second.char_id == char_id)
		{
			Mod_Multimap::iterator save_iter = iter;
			save_iter++;
			mod_records.erase(iter);
			iter = save_iter;
		}
		else
		{
			++iter;
		}
	}

	Integer_MapSet::iterator eset_iter = char_to_eset.find(char_id);
	if (eset_iter != char_to_eset.end())
	{
		set<int>::iterator set_iter;
		for (set_iter = eset_iter -> second.begin(); 
			 set_iter != eset_iter -> second.end(); ++set_iter)
		{
			eset -> depop_all(char_id, *set_iter);
		}

		char_to_eset.erase(eset_iter);
	}
}

// ==============================================================
// assign_spawngroup_eset -- bind a spawngroup to an eset (and therefore a char)
//
// The eset may not yet exist, but the binding takes effect.
// The spawngroup may need to be loaded
// ==============================================================
void QModifications::assign_spawngroup_eset(int char_id, int sg_id, int eset_id)
{
	if (zone)
	{
		SpawnGroup *sg = zone -> spawn_group_list.GetSpawnGroup(sg_id);
		if (sg == NULL)
		{
			database.LoadSpawnGroupsByID(sg_id, &zone->spawn_group_list);
			sg = zone -> spawn_group_list.GetSpawnGroup(sg_id);
			if (sg == NULL)
			{
				Log(Logs::General, Logs::Quests, "qmods: cannot load spawngroup %d",
					 sg_id);
				return;
			}
		}

		sg -> Assign_ESet(eset_id);
		eset -> assign_char(eset_id, char_id);
		
	}
}

// ==============================================================
// summon_item -- summon an item
//
// The items appear on the cursor.  This method is needed so that scripts can call it
//
// item_id:   The database key for the item to summon
// count:     The number of items (or charges) that should be summoned
// ==============================================================
bool
Quince::summon_item(Client *client, int item_id, int count)
{
	if (client != nullptr)
	{
		return client -> SummonItem(item_id, count);
	}

	return false;
}

string event_string(QuestEventID id)
{
	switch (id)
	{
		case EVENT_SAY:			return string("EVENT_SAY");
		case EVENT_ITEM:		return string("EVENT_ITEM");
		case EVENT_DEATH:		return string("EVENT_DEATH");
		case EVENT_SIGNAL:		return string("EVENT_SIGNAL");
		case EVENT_NULL:		return string("EVENT_NULL");
		case EVENT_SUBQUEST:	return string("EVENT_SUBQUEST");
		case EVENT_PROXIMITY:	return string("EVENT_PROXIMITY");
		case EVENT_DAMAGE:		return string("EVENT_DAMAGE");
		case EVENT_DROP:		return string("EVENT_DROP");
		case EVENT_ITEM_USE:			return string("EVENT_ITEM_USE");
		case EVENT_WAYPOINT_ARRIVE:		return string("EVENT_WAYPOINT_ARRIVE");
		default:
			return string("unknown");
	}
}
	

