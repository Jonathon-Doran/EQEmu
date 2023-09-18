#include "Quince.h"

using namespace std;

// ==============================================================
// Quest_Nodes represent one step/goal for a quest.  A node contains
// a set of triggers arranged by sequence.
// 
// Sequence 0 = activation triggers
// Sequence 1+ may be displayed in the quest journal
// ==============================================================

Quest_Node::Quest_Node(int quest_id, int node_id)
{
	this -> quest_id = quest_id;
	this -> node_id = node_id;

	string Trigger_Query = StringFormat(
		"SELECT trigger_id, sequence FROM `quince_triggers` "
		"WHERE quest_id=%d AND node_id=%d ORDER BY sequence", quest_id, node_id);

	ref_count = 1;				// start out with a single reference
	parent_trigger = -1;		// by default, there is no parent node

	//=========  Trigger Query ====================
	//
	auto results = database.QueryDatabase(Trigger_Query);
	if (! results.Success())
	{
		QuinceLog("Error loading trigger ids for node %d: %s", 
			node_id, results.ErrorMessage());
		return;
	}

	int trigger_count = results.RowCount();		// number of triggers loaded

	// Insert triggers as bindings (mapping of trigger id to sequence number)
	for (auto row : results)
	{
		int trigger_id = atoi(row[0]);
		int seq = atoi(row[1]);

		Trigger_Binding bind = {trigger_id, seq};
		trigger_ids.push_back(bind);
	}

	QuinceLog("%d triggers loaded for quest %d, node %d",
		trigger_count, quest_id, node_id);

	string Node_Query = StringFormat(
		"SELECT name, task, parent_trigger FROM `quince_questnodes` "
		"WHERE quest_id=%d AND node_id=%d", quest_id, node_id);

	//=========  Node Query ====================
	
	results = database.QueryDatabase(Node_Query);
	if (! results.Success())
	{
		QuinceLog("Error loading data for node %d: %s",
			node_id, results.ErrorMessage());
		return;
	}

	// There should only be one row of results, as node_id is a unique primary key.
	// quince_questnodes has names and tasks for node metadata
	for (auto row : results)
	{
		name = row[0];
		task = row[1];

		if (row[2] != NULL)
		{
			parent_trigger = atoi(row[2]);
			QuinceLog("parent trigger %d", parent_trigger);
		}

		QuinceLog("loaded trigger (%s) for node %d", name.c_str(), node_id);
	}
}

// ==============================================================
// add_reference -- increment the reference count for this node
//
// Quest_Nodes are stored in the NodeCache, and are reference counted.
// The reference counts are kept within the node, rather than in the cache.
// ==============================================================
void
Quest_Node::add_reference()
{
	ref_count++;
}

// ==============================================================
// remove_reference -- decrement the reference count for this node
// ==============================================================
int
Quest_Node::remove_reference()
{
	if (ref_count > 0)
	{
		ref_count--;
	}

	return ref_count;
}

