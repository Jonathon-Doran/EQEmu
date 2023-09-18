#include <algorithm>
#include "client.h"
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <iomanip>
#include <math.h>
// #include "../common/string_util.h"		unsure
#include "../common/random.h"
#include "../common/misc_functions.h"
#include "../common/eqemu_logsys.h"
#include "map.h"

#include "Quince_Events.h"
#include "Quince.h"

using namespace std;

string event_string(QuestEventID id);

// ==============================================================
// This constructor is PRIVATE to enforce the singleton pattern
// ==============================================================
Quince::Quince()
{
	// load available quest IDs for debugging
	string query("SELECT quest_id FROM quince_quests");

	auto quest_results = database.QueryDatabase(query);
	if (quest_results.Success())
	{
		for (auto row : quest_results)
		{
			quests.push_back(atoi(row[0]));
		}
	}
}

unique_ptr<Quince> Quince::_instance;

Quince&
Quince::Instance()
{
	if(_instance.get() == NULL)
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
// test_add - Debug routine to add a quest to the client
// ==============================================================
void Quince::test_add(Client *c, int root_node)
{
	int character_id = c -> CharacterID();

    if (client_state.find(character_id) == client_state.end())
	{
		c -> Message(Chat::White, "test_add: no client state for character %d", character_id);
		QuinceLog("test_add: no client state for character %d", character_id);
		return;
	}

	client_state[character_id].start_quest(root_node);
}

// ==============================================================
// zone_in:  Reload client state on zone-in
//
// Each zone has a separate zone executable, and each active client
// must zone in to begin interacting with this zone.
//
// Upon zone-in, the client should not be in the client_state map.
// The load_state call will create the client state!
// ==============================================================
void Quince::zone_in(Client *c, int zone)
{
    int character_id = c -> CharacterID();

    // manually turn on logging to debug zone startup logic
    LogSys.log_settings[Logs::LogCategory::Quests].log_to_file = Logs::DebugLevel::Detail;
    LogSys.log_settings[Logs::LogCategory::Quests].is_category_enabled = 1;

	QuinceLog("client %d zone in!", character_id);

	// Again, the client should NOT be in this map.  But avoid overwriting
    if (client_state.find(character_id) != client_state.end())
    {
		QuinceLog("already see a client state for char %d", character_id);
        return;
    }

	// create a Client_State object in the map, and load from the database
	// if the client has no quests, then load_state doesn't do anything
	
    client_state[character_id].load_state (c, zone);
}

// ==============================================================
// ==============================================================

void Quince::show_client(int client_id)
{
	Client_Map::const_iterator iter = client_state.find(client_id);

	if (iter != client_state.end())
	{
		iter -> second.display();
	}
	else
		QuinceLog("cannot find client %d", client_id);
}

// ==============================================================
// Return the root node for a quest (from the database).  Needed
// when a quest is added, since we don't keep this info around.  Plus
// it is unlikely that this same quest has been added recently.
// ==============================================================
int Quince::get_root_node(int quest_id)
{
	string query = StringFormat("SELECT node_id FROM quince_triggers"
			" WHERE quest_id=%d AND sequence=0", quest_id);
	auto results = database.QueryDatabase(query);
	
	int node_id = -1;		// default value

	// can have two root nodes
	if (results.Success() && results.RowCount() >= 1)
	{
		auto row = results.begin();
		node_id = atoi(row[0]);
	}
	else
	{
		QuinceLog("error loading root node for quest %d", quest_id);
	}

	return node_id;
}

string event_string(QuestEventID id)
{
	switch (id)
	{
		case EVENT_SAY:					return string("EVENT_SAY");
		case EVENT_ITEM:				return string("EVENT_ITEM");
		case EVENT_DEATH:				return string("EVENT_DEATH");
		case EVENT_SIGNAL:				return string("EVENT_SIGNAL");
		case EVENT_NULL:				return string("EVENT_NULL");
		case EVENT_SUBQUEST:			return string("EVENT_SUBQUEST");
		case EVENT_PROXIMITY:			return string("EVENT_PROXIMITY");
		case EVENT_DAMAGE:				return string("EVENT_DAMAGE");
		case EVENT_DROP:				return string("EVENT_DROP");
		case EVENT_ITEM_USE:			return string("EVENT_ITEM_USE");
		case EVENT_WAYPOINT_ARRIVE:		return string("EVENT_WAYPOINT_ARRIVE");
		default:
			return string("unknown");
	}
}
