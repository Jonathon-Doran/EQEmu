#include "Quince.h"
#include <memory>				// for unique_ptr

using namespace std;

unique_ptr<QNodeCache> QNodeCache::_instance;

QNodeCache&
QNodeCache::Instance()
{
	if (_instance.get() == NULL)
	{
		_instance.reset (new QNodeCache());
	}

	return *_instance;
}

// ==============================================================
// Need a constructor in order to make it protected
// ==============================================================

QNodeCache::QNodeCache()
{
}

// ==============================================================
// load_node -- load a node into the cache
//
// Increments the reference count if already in the cache.
// ==============================================================
void
QNodeCache::load_node(int node_id)
{
	Node_Map::iterator iter = nodes.find(node_id);

	if (iter != nodes.end())
	{
		// add a reference to the Quest_Node
		iter -> second.add_reference();
		return;
	}

	int quest_id;

	// look up the id of the quest with this node
	if (! quest_for(node_id, quest_id))
	{
		QuinceLog("load_node: node %d not found in cache", node_id);

		if (! quest_from_db(node_id, quest_id))
		{
			QuinceLog("load_node: node %d not found in database", node_id);
			return;
		}
	}

	QuinceLog("Node Cache load_node:  preparing to create Quest_Node for node %d", node_id);

	// insert a new Quest_Node object into 'nodes'
	// one reference added upon create
	
	nodes.insert(pair<int, Quest_Node>
		(node_id, Quest_Node(quest_id, node_id)));
}

// ==============================================================
// unload_node -- remove a reference to a node
//
// When the reference count hits zero, remove from the cache
// ==============================================================

void
QNodeCache::unload_node(int node_id)
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

	QuinceLog("unload_node:  attempt to unload non-present node %d", node_id);
}

// ==============================================================
// quest_from_db -- look up the quest-id for a node
//
// Node is not in the cache, so load info from the database
// ==============================================================

bool
QNodeCache::quest_from_db(int node_id, int& value)
{
	string query = StringFormat(
		"SELECT quest_id FROM `quince_questnodes` WHERE node_id=%d", node_id);
	int quest_id = -1;

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		QuinceLog("quest_from_database:  error loading quest");
		return false;
	}

	// only one quest has this node, so only one row possible
	auto row = results.begin();
	value = atoi(row[0]);
	return true;
}


// ==============================================================
// node_present -- determine if a node is in the cache
// ==============================================================
bool
QNodeCache::node_present(int node_id)
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
// quest_for -- determine the quest for a node
//
// node_id:   node to search for
// quest_id:  returned value (valid if returning true)
// ==============================================================

bool
QNodeCache::quest_for(int node_id, int& quest_id)
{
	// Node_Map is a map from node_id to Quest_Node
	Node_Map::const_iterator iter = nodes.find(node_id);

	if (iter != nodes.end())
	{
		quest_id = iter -> second.getQuest();
		return true;
	}

	return false;
}

// ==============================================================
// Trigger Cache
//
// Note that triggers have system-unique IDs, so we do not need to
// know which quest they belong to.
// ==============================================================
unique_ptr<QTriggerCache> QTriggerCache::_instance;

QTriggerCache&
QTriggerCache::Instance()
{
	if (_instance.get() == NULL)
	{
		_instance.reset (new QTriggerCache());
	}

	return *_instance;
}

QTriggerCache::QTriggerCache()
{
}

// ==============================================================
// load_triggers_at_seq -- load all triggers at a sequence number
//
// A node can have multiple trigger at a sequence number, so load all.
// For example, a series of signal handlers.
// ==============================================================

ID_List 
QTriggerCache::load_triggers_at_seq(int node_id, int seq, int character_id)
{
	QuinceLog("load_triggers_at_seq %d for node %d", seq, node_id);

	ID_List trigger_ids;

	string query = StringFormat(
		"SELECT trigger_id FROM `quince_triggers` WHERE node_id=%d AND sequence=%d",
			node_id, seq);
	
	auto results = database.QueryDatabase(query);
	if (! results.Success())
	{
		QuinceLog("load_triggers_at_seq: %s", results.ErrorMessage());
		return trigger_ids;
	}

	// Load each trigger
	for (auto row : results)
	{
		int trigger_id = atoi(row[0]);
		load_trigger(trigger_id, seq, character_id);
		trigger_ids.push_back(trigger_id);
	}

	QuinceLog("found %d triggers", trigger_ids.size());
	return trigger_ids;
}

// ==============================================================
// load_trigger -- load a single trigger into the cache
//
// May cause a new QuinceTrigger to be created and loaded.
// Triggers in the cache are reference counted.
//
// There are various maps for looking up info:
//
// 		trigger-id => QuinceTrigger
// 		trigger-id => node-id
// 		trigger-id => sequence number
// 		trigger-id => character-id
// 		zone-id    => set of trigger-ids
// ==============================================================

void
QTriggerCache::load_trigger(int trigger_id, int seq, int character_id)
{
	// first, check to see if the trigger is already cached
	Integer_Map::iterator iter = ref_counts.find(trigger_id);

	if (iter != ref_counts.end())
	{
		iter -> second++;
		return;
	}

	// if not in cache, load a new trigger
	QuinceTrigger *trigger = QuinceTriggers::Instance().load_trigger(trigger_id);

	if (trigger == NULL)
	{
		QuinceLog("load_trigger:  failed to load trigger %d", trigger_id);
		return;
	}

	triggers[trigger_id] = trigger;
	node_ids[trigger_id] = trigger -> get_node();
	ref_counts[trigger_id] = 1;
	sequence[trigger_id] = seq;
	int zone = trigger -> getZone();

	// add to map of triggers => associated client
	trigger_owner.insert(pair<int,int> (trigger_id, character_id));

	// add to multi-map of zone-id => trigger
	zone_map.insert(pair<int,int> (zone, trigger_id));

	// FIXME:  implement
	// initialize_trigger(trigger);
	QuinceLog("loaded trigger %d (%s)", trigger_id,
		trigger -> getName().c_str());
	QuinceLog("trigger type = %s", event_string(trigger -> getType()).c_str());
}

// ==============================================================
// node_for -- look up the node associated with a trigger
// ==============================================================

bool
QTriggerCache::node_for(int trigger_id, int& node_id)
{
	Integer_Map::const_iterator iter = node_ids.find(trigger_id);

	if (iter == node_ids.end())
	{
		return false;
	}

	node_id = iter -> second;
	return true;
}

// ==============================================================
// init_sat_for_trigger -- determine if the trigger is initially satisfied
// ==============================================================
bool
QTriggerCache::init_sat_for_trigger(int trigger_id)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		QuinceLog("TriggerCache:  init_sat_for_trigger:  trigger %d not in map", trigger_id);
		return true;
	}

	return (iter -> second -> initSatisfied());
}

// ==============================================================
// get_subroutine -- return the subroutine name for a trigger
//
// This is the name of the PERL function which is ran when the
// trigger fires.
// ==============================================================
bool
QTriggerCache::get_subroutine(int trigger_id, string& name)
{
	Trigger_Map::iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	name = iter -> second -> getSubroutine();
	return true;
}

// ==============================================================
// getType -- Return the type for a trigger.
//
// Mainly used so that subquest triggers can be treated specially.
// Subquest triggers fire differently, and do not appear in activity
// lists.
// NULL triggers also fire immediately.
// ==============================================================
bool
QTriggerCache::getType(int trigger_id, QuestEventID& type)
{
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	type = iter -> second -> getType();
	return true;
}

bool
QTriggerCache::get_zone(int trigger_id, int& zone)
{
	
	Trigger_Map::const_iterator iter = triggers.find(trigger_id);

	if (iter == triggers.end())
	{
		return false;
	}

	zone = iter -> second -> getZone();
	return true;
}

