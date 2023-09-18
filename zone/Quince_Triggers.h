/*  EQEMu:  Everquest Server Emulator
    Copyright (C) 2001-2003  EQEMu Development Team (http://eqemulator.net)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY except by those people which sell it, which
    are required to give you total support for your newly bought product;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR
    A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

      You should have received a copy of the GNU General Public License
      along with this program; if not, write to the Free Software
      Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef QUINCE_TRIGGERS_H
#define QUINCE_TRIGGERS_H

#include "../common/types.h"
#include "event_codes.h"
#include <regex.h>
#include <list>
#include <vector>
#include <string>
#include <set>
#include <memory>	// for unique_ptr
//#include "events.h"
#include "embparser.h"

class Event;
class Event_Damage;

/*
	EntityType describes the various types of entities that might be involved in
	quests.  These represent the nouns used when describing a quest.
*/

typedef enum
{
	Entity_None = 0,
	Entity_NPC = 1,
	Entity_Item = 2,
	Entity_Location = 3,
	Entity_Player = 4,
	Entity_Door = 5,
	Entity_Group = 6,
	Entity_Raid = 7,
	Entity_NPC_Type_ID = 8,			// not an actual entity, but allows class reuse
	Entity_Spawngroup = 9
} EntityType;

/*
	All IDs are 32-bit integers.  The EntityType determines what table the ID
	will refer to.  Entity_NPC indicates an npcID (npc_types table), 
	Entity_Item indicates an itemID (items table).  Entity_Location indicates 
	a world location (locations table).
*/
struct EntityDescriptor
{
	EntityType type;
	int32 entityID;
};

struct Waypoint_Data
{
	float x;
	float y;
	float z;
	float h;
};

class NPC;
class Mob;

/*
	Abstract base class for all triggers.

*/
class QuinceTrigger
{
	private:
		QuestEventID type;		// type of trigger
		int32 quest_id;			// key to lookup script name
		std::string subroutine;
		int32 node_id;

		std::string task;
		bool repeatable;
		bool init_satisfied;


	protected:
		int32 trigger_id;
		int32 zone_id;			// trigger only applies to one zone
		int count;				// waypoint triggers need to adjust count

		void set_perl_variables(int char_id);

	public:
		QuinceTrigger(int id, QuestEventID t, int z, int q, std::string sub,
			std::string t_task, int c);

		virtual ~QuinceTrigger();

		inline std::string getName() const	{return subroutine;}
		inline std::string getTask() const	{return task;}
		inline void set_node(int node)		{node_id = node;}
		inline int get_node() const			{return node_id;}
		inline int get_count() const		{return count;}
		const char* entitytype_to_string(EntityType);

		std::string questScript();
		std::string zoneName();
		inline std::string getSubroutine()		{return subroutine;}
		inline int getZone() const				{return zone_id;}
		inline bool isRepeatable() const		{return repeatable;}
		inline void set_repeatable(bool val)	{repeatable = val;}
		inline void set_satisfied(bool val)		{init_satisfied = val;}
		inline bool initSatisfied() const		{return init_satisfied;}

		bool entitiesEqual(EntityDescriptor*, EntityDescriptor*);
		inline int32 getQuestID() const	{return quest_id;}
		inline QuestEventID getType() const		{return type;}
		std::string packageName();
		virtual void process(Event *) = 0;
		inline int32 getTriggerID()		{return trigger_id;}

		virtual void activate()		{}
		virtual bool is_boolean()		{return false;}
};

class QNull : public QuinceTrigger
{
	public:
		QNull(int id, int quest, std::string name);
		void process(Event *) {};
};

/*
	Q_Damage:  damage an entity by at least "percent" of its HP.  A quest 
	update is triggered when health goes below this value.  100% indicates 
	death/total destruction.  A special case:  0% indicates any amount of 
	damage.
*/
class QDamage : public QuinceTrigger
{
	private:
		EntityDescriptor entity;
		int8 percent;

		bool readyToFire(Event_Damage *);

	public:
		QDamage(int id, EntityDescriptor *npc, int threshold,
			int quest, std::string subroutine, std::string task);
		void activate();				// turn on reporting for this entity
		void process(Event *);
};

class QSay : public QuinceTrigger
{
	private:
		EntityDescriptor entity;
		regex_t regex;

	public:
		QSay(int id, EntityDescriptor *target, std::string pattern, int zone, 
			 int quest, std::string subroutine, std::string task);
		void process(Event *);
		void print();
};

// could be item given to player, or to npc
class QItem : public QuinceTrigger
{
	private:
		EntityDescriptor receiver;		// who receives item
		int32 itemID;					// item to receive

	public:
		QItem(int id, EntityDescriptor *, int32, int count, int zone, 
			int quest, std::string subroutine, std::string task);
		void process(Event *);
		bool usesNPC(int npcType);
};

class QDrop : public QuinceTrigger
{
	private:
		int zone;
		int32 itemID;					// item to drop

		float dist_2;		// neg values to trigger when away from center
		bool invert;
		float x, y, z;		// center of region

	public:
		QDrop(int id, int32 item, int count, int zone, float x, float y, 
			float z, float dist, int quest, std::string subroutine, 
			std::string task);
		void process(Event *);
};

class QSpawn : public QuinceTrigger
{
	private:
		int32 npcID;
	public:
		QSpawn(int id, int32 npcID, int zone, int quest, 
			std::string subroutine, std::string task);
		void process(Event *);
};

class QSignal : public QuinceTrigger
{
	private:
		int32 signal;
	public:
		QSignal(int id, int32 signal, int zone, int quest, 
			std::string subroutine, std::string task);
		void process(Event *);
};

class QDeath : public QuinceTrigger
{
	private:
		int32 npcID;
		int32 spawngroup;
	public:
		QDeath(int id, int32 npcID, int32 spawngroup, int count, int zone, int quest, 
			std::string subroutine, std::string task);
		void process(Event *);
};


class QProximity : public QuinceTrigger
{
	private:
		float dist_2;		// negative values to trigger when away from center
		bool invert;
		float x, y, z;		// center of region
		bool inside;		// state of trigger (may refire multiple times)
		int32 npcID;
		int esetID;

	public:
		QProximity(int id, int zone, int quest, std::string subroutine, 
			std::string task, float x, float y, float z, 
			int npc_id, int eset_id, float distance);
		QProximity(int id, int zone, int quest, std::string subroutine, 
			std::string task, int32 npcID, float distance);

		void process(Event *);
};

// right click of item (with target)
class QItemUse : public QuinceTrigger
{
	private:
		int32 item_id;
	public:
		QItemUse(int id, int item_id, int count, int zone, int quest_id,
			std::string subroutine, std::string task);
		void process(Event *);
};

class QSubquest : public QuinceTrigger
{
	private:
		int32 parent_trigger;
		int sub_node;
	public:
		QSubquest(int id, int node_id, int parent, int quest, std::string name,
			std::string task);
		void process(Event *);
		inline int getParent() const	{return parent_trigger;}
		inline int get_sub_node() const	{return sub_node;}
};

class QWaypoint_Arrive : public QuinceTrigger
{
	private:
		std::vector<Waypoint_Data> waypoints;
		int npc_id;
	public:
		QWaypoint_Arrive(int id, int npc_id, int count, int zone, int quest_id, 
			std::string name, std::string task);
		void process(Event*);
		bool is_boolean()	{return true;}			// fake the activity count
		
};

/*
	QuinceTriggers is a collection of QuinceTrigger objects.
*/

class QuinceTriggers
{
	private:
		static std::unique_ptr<QuinceTriggers> _instance;
		std::vector< std::list<QuinceTrigger *> > trigger_list;
		std::set<int> loadedQuests;

		PerlembParser *perl_parser;
		Client *active_client;					// valid when trigger firing

		// void loadQuest(int quest_id);
	protected:
		QuinceTriggers();
	public:
		static QuinceTriggers& Instance();
		void reset();
		inline Client *getClient()		{return active_client;}
		bool  processEvent(Event *event);
		std::string questScript(int quest_id);
		std::string scriptBase(int quest_id);
		std::string startZone(int quest_id);
		std::string packageName(int quest_id);

		QuinceTrigger *load_trigger(int trigger_id);
		void unload_trigger(QuinceTrigger *trigger);

		bool isLoaded(int quest_id);
		void markLoaded(int quest_id);

		bool HasItemTrigger(int npcType);

		void registerParser(PerlembParser *p)	{perl_parser = p;}
		inline PerlembParser *getParser() {return perl_parser;}
};

#endif
