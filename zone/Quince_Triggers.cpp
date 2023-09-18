#include "Quince.h"
#include "Quince_Triggers.h"
#include "Quince_Events.h"

using namespace std;

// ==============================================================
// This is the abstract base class for all triggers.
// Do not confuse it with QuinceTriggers which is concrete.
//
// Some common virtual methods:
//
// print:		Log the trigger (print out members)
// process:		Process an event of the appropriate type
// ==============================================================
QuinceTrigger::QuinceTrigger(int trigger_id, QuestEventID event_type, int zone_id,
			int quest_id, string subroutine, string task, int count)
{
	this -> trigger_id = trigger_id;
	this -> type = event_type;
	this -> zone_id = zone_id;
	this -> quest_id = quest_id;
	this -> subroutine = subroutine;
	this -> task = task;
	this -> count = count;
}

QuinceTrigger::~QuinceTrigger()
{
	QuinceLog("deleting trigger %d", trigger_id);
}

static map<EntityType, string> entity_to_string = {{Entity_None, "none"}, {Entity_NPC, "npc"},
		{Entity_Item, "item"}, {Entity_Location, "location"}, {Entity_Player, "player"},
		{Entity_Door, "door"}, {Entity_Group, "group"}, {Entity_Raid, "raid"},
		{Entity_NPC_Type_ID, "npc-type-id"}, {Entity_Spawngroup, "spawngroup"}};

// ==============================================================
// QNull -- trigger which fires upon activation
//
// Previously, no events were generated for this, but the generator
// does create PERL scripts for these.
// ==============================================================
QNull::QNull(int id, int quest, string name)
	: QuinceTrigger(id, EVENT_NULL, 0, quest, name, string("null"), 0)
{
}

// ==============================================================
// QSay -- fires when an entity/NPC says something
//
// entity:   The entity in question
// regex:	 A regular expression defining the target text
//
// The trigger fires when the entity says something which matches
// the regular expression.
// ==============================================================
QSay::QSay(int id, EntityDescriptor *target, string pattern, int zone,
	int quest, string subroutine, string task)
	: QuinceTrigger(id, EVENT_SAY, zone, quest, subroutine, task, 1)
{
	entity.type = target -> type;
	entity.entityID = target -> entityID;

	QuinceLog("qsay %s", task.c_str());
	QuinceLog("regex %s", pattern.c_str());
	regcomp(&regex, pattern.c_str(), REG_EXTENDED|REG_ICASE);
}

// ==============================================================
// 		process -- Process an event of type EVENT_SAY
// ==============================================================
void
QSay::process(Event *event)
{
	if (event -> GetType() != EVENT_SAY)
	{
		QuinceLog("QSay asked to process %s", event_string(event->GetType()).c_str());
		return;
	}

	QuinceLog("QSay::process -- processing a QSay");
	Event_Say *sayEvent = (Event_Say *) event;

	// ignore event if character hasn't been associated
	// (or is an activation trigger)
	
#if 0
	if (! Quince::Instance().is_associated(trigger_id, sayEvent -> getCharacterID()))
	{
		QuinceLog("trigger %d not associated with character %d",
			trigger_id, sayEvent -> getCharacterID());
		return;
	}

	QuinceLog("speech event: %s, compare entityID %d against trigger entity %d",
		sayEvent->getText().c_str(), sayEvent->getNPCID(), entity.entityID);

	if (sayEvent->getNPCID() != entity.entityID)
	{
		QuinceLog("entity ID/type mismatch");
		return;
	}

	if (regexec(&regex, sayEvent->getText().c_str(), (size_t) 0, NULL, 0) != 0)
	{
		QuinceLog("regex mismatch");
		return;
	}

	QuinceLog("trigger firing:  script %s, subroutine %s",
		questScript().c_str(), getSubroutine().c_str());
	Quince::Instance().set_trigger_state(trigger_id,
		sayEvent->getCharacterID(), 1);
	set_perl_variables(sayEvent->getCharacterID());

	PerlembParser *parser = QuinceTriggers::Instance().getParser();

	// load the quest, if it hasn't already been
	if (! QuinceTriggers::Instance().isLoaded(getQuestID()))
	{
		// FIXME:  implement
	}

	// send eventcommon
	// notify clients

#endif
}

// ==============================================================
// 		print -- describe this trigger (for debugging)
// ==============================================================
void
QSay::print()
{
	EntityType type = entity.type;
	QuinceLog("QSay %s %d", entity_to_string[type].c_str(), entity.entityID);

}

unique_ptr<QuinceTriggers> QuinceTriggers::_instance;

QuinceTriggers&
QuinceTriggers::Instance()
{
	if (_instance.get() == NULL)
	{
		_instance.reset (new QuinceTriggers());
	}

	return *_instance;
}

QuinceTriggers::QuinceTriggers()
{
	QuinceLog("creating QuinceTriggers");

	// should be registered in main
	perl_parser = NULL;

	// FIXME:   is QuinceTriggers::reset ever called?
	// 			Clearing out all triggers is extreme
}

// ==============================================================
// isLoaded -- determine if a quest's Perl script has been loaded
// ==============================================================
bool
QuinceTriggers::isLoaded(int questID)
{
	ID_Set::iterator iter = loadedQuests.find(questID);

	if (iter == loadedQuests.end())
	{
		QuinceLog("QuinceTriggers::isLoaded: quest %d is NOT loaded", questID);
		return false;
	}

	QuinceLog("QuinceTriggers::isLoaded: quest %d currently loaded", questID);
	return true;
}

// ==============================================================
// markLoaded -- mark a quest as having its script loaded
// ==============================================================
void
QuinceTriggers::markLoaded(int questID)
{
	loadedQuests.insert(questID);
}

// ==============================================================
// questScript -- return the filename for a quest's script
//
// Note that questIDs are globally unique.  Anytime a quest is
// added to the database (its triggers are loaded) the nnext ID
// in sequence is added.  Thus, quests are not tied to zones.
// ==============================================================

string
QuinceTriggers::questScript(int questID)
{
	ostringstream oss;
	oss << "quest_" << questID << ".pl";
	return oss.str();
}

// ==============================================================A
// packageName -- return the name of the quest's package
//
// The package is automatically created, and only needs to be
// unique.  The purpose is to create a new namespace for each
// quest so that we don't need to worry about symbol clashes.
// ==============================================================

string
QuinceTriggers::packageName(int questID)
{
	ostringstream oss;
	oss << "quince_quest_" << questID;
	return oss.str();
}

// ==============================================================
// load_trigger -- load a trigger from the database
// ==============================================================

QuinceTrigger *
QuinceTriggers::load_trigger(int trigger_id)
{
	QuinceLog("QuinceTriggers loading trigger %d", trigger_id);

	string query = StringFormat(
		"SELECT trigger_type, zone, npc, item, count, misc_id,  "
		"regex, loc_x, loc_y, loc_z, quest_id, trigger_name, "
		"task, node_id, sub_node, threshold, repeatable, "
		"satisfied, signal_num FROM `quince_triggers` WHERE trigger_id=%d",
		trigger_id);

	auto results = database.QueryDatabase(query);

	if (! results.Success())
	{
		QuinceLog("load_trigger: error loading trigger: %s",
			results.ErrorMessage());
		return NULL;
	}

	// create a trigger from these values
	
	QuinceTrigger *trigger = NULL;

	// there should be only one row, as trigger_id is the primary key
	for (auto row : results)
	{
		string trigger_type = row[0];
		int zone = atoi(row[1]);

		int npc = -1;
		if (row[2] != NULL)
		{
			npc = atoi(row[2]);
		}

		int item = atoi(row[3]);
		int count = atoi(row[4]);
		int misc_id = atoi(row[5]);

		string regex("");
		if (row[6] != NULL)
		{
			regex = row[6];					// may be NULL
		}
		
		float loc_x = atof(row[7]);
		float loc_y = atof(row[8]);
		float loc_z = atof(row[9]);
		int quest_id = atoi(row[10]);
		string trigger_name;
		if (row[11] != NULL)
		{
			trigger_name = row[11];
		}
		string task;
		if (row[12] != NULL)
		{
			task = row[12];				// may be NULL
		}
		int node_id = atoi(row[13]);

		int sub_node = 0;					// may be NULL
		if (row[14] != NULL)
		{
			sub_node = atoi(row[14]);
		}
		float threshold = atof(row[15]);

		bool repeatable = false;			// optional
		if (row[16] != NULL)
		{
			repeatable = (*row[16] == 1);
		}

		bool satisfied = false;				// optional
		if (row[17] != NULL)
		{
			satisfied = (*row[17] == 1);
		}

		int signal = 0;						// may be NULL
		if (row[18] != NULL)
		{
			signal = atoi(row[18]);
		}

		if (repeatable)
		{
			QuinceLog("trigger %d is repeatable", trigger_id);
		}
		if (satisfied)
		{
			QuinceLog("trigger %d is satisfied", trigger_id);
		}

		// FIXME:  Do I want to represent this as an enum?
		if (trigger_type.compare("match") == 0)
		{
			
		}
		else if (trigger_type.compare("null") == 0)
		{
			trigger = new QNull(trigger_id, quest_id, trigger_name);
		}

		QuinceLog("load_trigger sees type %s", trigger_type.c_str());
		return trigger;
	}

	return NULL;
}
