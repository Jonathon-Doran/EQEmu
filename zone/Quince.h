#ifndef QUINCE_H
#define QUINCE_H

#include <map>
#include <memory>	// for unique_ptr
#include <set>
#include <vector>
#include "entity.h"
#include "Quince_Triggers.h"
#include "Quince_Clients.h"
#include "event_codes.h"


#define QuinceLog(...)  { \
                            std::string value = StringFormat(__VA_ARGS__); \
                            std::string prefix("[QUINCE] "); \
                            Log(Logs::General, Logs::Quests, (prefix+value).c_str()); \
                        }

extern std::string event_string(QuestEventID id);
// forward decls
class Client;
class Quest_Node;
class Client_State;
class Timer_Binding;
class Entity_Registration;
class Variable_Binding;

// ==============================================================
// Helper class to manage sets of spawns in scripts.  Mainly
// to allow all quest-related entities to be despawned.
//
// This is transient info (does not need to be stored in the database)
// ==============================================================

struct ESet_Record
{
	int char_id;
	int quest_id;
	int eset_id;
	EntityType type;
	int entity_id;
};
typedef std::list<ESet_Record> 			ESet_List;

typedef std::vector<QuinceTrigger *> 	Trigger_List;
typedef std::map<int, Quest_Node> 		Node_Map;
typedef std::map<int, int> 				Integer_Map;
typedef std::multimap<int, int> 		Integer_Multimap;
typedef std::map<int, std::set<int> >	Integer_MapSet;
typedef std::map<int, Client_State> 	Client_Map;
typedef std::map<int, QuinceTrigger *> 	Trigger_Map;
typedef std::list<int>	 				ID_List;
typedef std::set<int>					ID_Set;
typedef std::map<int, int> 				Trigger_ID_Map;
typedef std::map<int, int> 				Node_ID_Map;
typedef std::map<int, bool>				Bool_Map;
typedef std::multimap<int, int> 		Trigger_ID_Set;
typedef std::vector<std::string> 		String_List;
typedef std::map<int, std::string> 		String_Map;
typedef std::list<Timer_Binding *>		Timer_List;
typedef std::map<std::string, Variable_Binding>	Var_Pool;		// variable => value
typedef std::map<uint32, Var_Pool>		Var_Map;		// char_id => pool
typedef std::map<int, Trigger_List>		Zone_Trigger_Map;	// zone => triggers
typedef std::multimap<int, int> 		Entity_Map;
typedef std::map<int, EntityDescriptor> ESet_Entities;
typedef std::map<int, ESet_Entities> 	ESet_Map;
typedef std::map<int, ESet_Map>			Quest_Entity_Map;	// char ->

class Entity_Set;
class Mob_Set;
class Loot_Modification;
class Item_Restriction;

class Quince
{
	private:
		static std::unique_ptr<Quince> _instance;

		Client_Map client_state;
		Timer_List active_timers;
		Var_Map variables;
		Entity_Registration *registrations;
		ID_List quests;								// available quest IDs

	protected:
		Quince();

	public:
		static Quince& Instance();

		int get_root_node(int quest_id);
		void reset(uint32 character_id);

		void zone_in(Client *c, int zone_id);		// load new state
		void zone_out(Client *c);					// delete current state

		void test_add(Client *c, int node_id);
		void test_fire(Client *c, int trigger_id);
		void test_complete(Client *c, int trigger_id);

		void show_selector(Client *c, int quest_id);
		void send_task(Client *c, int quest_id);

		void list(Client *c);
		bool accept_quest(Client *c, int quest_id, int npc_id);
		void cancel_quest(Client *c, int seq_id);
		void fail_quest(Client *c, int quest_id);

		void show_client(int cid);

		// called by triggers upon firing:  advance quests, update client journal
		void notify_clients(int trigger_id, int character_id = 0);

		// to allow triggers to know whether to process an event
		bool is_associated(int trigger_id, int client_id);
		bool get_quest_title(int quest_id, std::string& title);

		void process_global_event(Event *event, int quest_id);
		int get_trigger_state(int trigger_id, int client_id);
		void set_trigger_state(int trigger_id, int client_id, int state);
		bool is_trigger_satisfied(int trigger_id, int client_id);

		// spawn group tests
		void start_spawn(unsigned int spawngroup);
		void stop_spawn(unsigned int spawngroup);
		void info_spawn(uint32 spawngroup);
		void spawn_nearby(int char_id, int quest_id, int eset_id, uint32 spawngroup, float x, float y, float z, uint32 count);

		void activate_trigger(int character, int trigger);
		void deactivate_trigger(int character, int trigger);

		void start_timer(uint32 char_id, uint32 quest, uint32 signal, uint32 time);
		void stop_timer(uint32 char_id, uint32 quest, uint32 signal);
		void stop_all_timers(uint32 char_id);
		void process_timers();
		void process();			// timed events
		void send_signal(uint32 char_id, uint32 quest, uint32 signal);

		bool summon_item(Client *c, int item_id, int count);

		// variables (transient)
		void store_value(uint32 char_id, uint32 quest, char *variable, int value);
		int retrieve_value(uint32 char_id, uint32 quest, char *variable);
		void remove_all_vars(uint32 char_id);

		// registrations
		void register_watch(QuestEventID type, uint32 npc_id, uint32 char_id);
		void remove_watch(QuestEventID type, uint32 npc_id, uint32 char_id);
		bool check_watch(QuestEventID type, uint32 npc_id);
		ID_List clients_for(QuestEventID type,  uint32 npc_id);
		Client *get_client(uint32 char_id);

		void restrict_item_target(int char_id, int quest_id, int item_id,
			int eset_id);
		void remove_item_restrict(int char_id, int quest_id, int item_id);

		// mset utilities
		void exchange_loot(int char_id, int quest_id, int mset_id,
			int orig_item, int new_item, int chance);
		void remove_exchange(int char_id, int quest_id, int mset_id);

		// rollback
		void set_rollback(int char_id, int node_id, int seq);
		void clear_rollback(int char_id, int node_id);

		void init_waypoints(int char_id, int npc_eid);
		NPC_WPs get_client_waypoint_info(int npc_eid);
		void remove_client_waypoint_info(int npc_eid);
};

// ==============================================================
// Trigger bindings associate a sequence# with a trigger-id
//
// This allows nodes to maintain a list of bindings rather than
// keeping all triggers loaded.
// ==============================================================

struct Trigger_Binding
{
	int trigger_id;
	int seq;
};

struct Binding_Sorter
{
	bool operator() (Trigger_Binding const & lhs, Trigger_Binding const & rhs)
	{
		return lhs.seq < rhs.seq;
	}
};

struct Timer_Binding
{
	Timer timer;
	unsigned int quest;
	unsigned int signal;
	unsigned int character_id;

	Timer_Binding(unsigned int q, unsigned int s)
		: timer(0)
	{
		quest = q;
		signal = s;
	}
};

struct Variable_Binding
{
	unsigned int quest;
	std::string key;
	unsigned int character_id;
	int value;
};

class QNodeCache
{
	private:
		Node_Map nodes;
		static std::unique_ptr<QNodeCache> _instance;

	protected:
		QNodeCache();

	public:
		static QNodeCache& Instance();
		void reset();

		inline int num_nodes()			{return nodes.size();}

		bool node_present(int node_id);
		bool quest_for(int node_id, int& quest);
		bool quest_from_db(int node_id, int& quest);

		void load_node(int node_id);
		void unload_node(int node_id);

		ID_List trigger_ids_at_seq(int node, int seq);
		ID_List node_ids_for_quest(int quest);

		bool getName(int node_id, std::string& value);
		void display(Client *c) const;

		bool get_parent_trigger(int node_id, int& trigger_id);
};

class QuinceTriggerCache
{
	private:
		Trigger_Map triggers;
		Integer_Map node_ids;			// trigger_id => node_id
		Integer_Map ref_counts;			// trigger_id => reference count
		Integer_Map sequence;			// trigger_id => sequence
		Integer_Multimap zone_map;		// zone => trigger_id
		Integer_Multimap trigger_owner;	// trigger_id => character_id
		Integer_Map activated_quest;	// trigger_id => quest_id

		static std::unique_ptr<QuinceTriggerCache> _instance;
		Zone_Trigger_Map activation_triggers;
		ID_List activation_ids;			// trigger ids
		ID_List polled_triggers;		// polled activation triggers

		void remove_trigger_owner(int trigger_id, int character_id);
		void initialize_trigger(QuinceTrigger *);
	protected:
		QuinceTriggerCache();

	public:
		static QuinceTriggerCache& Instance();
		void reset();

		inline int num_triggers()		{return triggers.size();}

		void load_trigger(int trigger_id, int seq, int character_id);
		void unload_trigger(int trigger_id, int character_id);

		void load_activation_triggers(int zone);
		ID_List load_triggers_at_seq(int node, int seq, int character_id);

		bool get_sub_node(int trigger_id, int& sub_node);
		// need type for activation, since subquests handled differently
		bool getType(int trigger_id, QuestEventID& type);
		bool get_task(int trigger_id, std::string& task);
		bool get_zone(int trigger_id, int& zone);
		bool get_seq(int trigger_id, int& seq);
		bool get_count(int trigger_id, int& count);
		bool get_subroutine(int trigger_id, std::string& name);

		// used when zones shutdown to ensure all assets removed
		void unload_all_triggers(int zone);
		void remove_active_from_database(int trigger_id, int char_id);

		bool node_for(int trigger_id, int& node_id);
		bool quest_for(int trigger_id, int& quest_id);
		inline int activation_zones()		{return activation_triggers.size();}

		bool init_sat_for_trigger(int trigger_id);
		bool is_activation_trigger(int trigger_id);
		bool is_repeatable_trigger(int trigger_id);
		bool is_boolean(int trigger_id);

		ID_List associated_clients(int trigger_id);

		// inject an event into a trigger
		void process(int trigger_id, Event *event);

		void process_global_activation(Event *event);

		void display(Client *c) const;
};

// ==============================================================
// The main reason for Quest Nodes is to maintain a sequence of
// triggers within a quest stage.
//
// This is shared topology information (NOT client state).  Clients may
// refer to quest nodes to learn what they should do next.
// ==============================================================
class Quest_Node
{
	private:
		int quest_id;
		int node_id;

		std::string name;
		std::string task;

		int ref_count;		// unload on transition from 1 to 0
							// activation triggers not stored in Quest_Node

		int parent_trigger;								// only used with subquests

		std::vector<Trigger_Binding> trigger_ids;		// sorted by seq #
		Trigger_Map triggers;

		void load_trigger_ids();

	public:
		Quest_Node(int quest_id, int node_id);

		void add_reference();
		int remove_reference();
		inline int num_references() const	{return ref_count;}

		void load_node(int node_id);

		ID_List trigger_ids_at_seq(int seq);

		inline std::string getName() const	{return name;}
		inline std::string getTask() const	{return task;}
		inline int getQuest() const			{return quest_id;}
		inline int getID() const			{return node_id;}
		inline int getParent() const		{return parent_trigger;}
};

// ==============================================================
// Registration of entities allows the server to limit the number of
// events that are processed.  Probably only needed for global events.
// ==============================================================

struct Event_Registration
{
	QuestEventID type;
	int entity;
	int char_id;
};

class Entity_Registration
{
	private:
		std::list<Event_Registration> entries;
	public:
		bool present(QuestEventID type, int);
		void insert(QuestEventID type, int entity, int char_id);
		void remove(QuestEventID type, int entity, int char_id);
		void remove_all(int char_id);		// e.g. zone-out
		ID_List clients_for(QuestEventID type, int);
};

// ==============================================================
// Helper class to manage sets of spawns in scripts.  Mainly
// to allow all quest-related entities to be despawned.
//
// This is transient info (does not need to be stored in the database)
// ==============================================================

// ==============================================================
// A container for entities (items, npcs)
//
// These types of entities have unique ids (the ids are allocated from the
// same pool), so there is no need to track the entity type.
//
// An entity is either in a set, or is not.  ESet ids are server-unique,
// so a particular ID refers to a single quest on a single server.
// ==============================================================
class Entity_Set
{
	private:
		// eset_id -> list<ESet_Record>
		std::map<int, ESet_List> entities;
		std::map<int, int> eset_to_char;

	public:
		bool exists(int eset_id);
		void assign_char(int eset_id, int char_id);
		void insert(int char_id, int eset_id, EntityType type, int entity_id);
		void insert(int eset_id, EntityType type, int entity_id);

		void remove(int char_id, int eset_id, int entity_id);

		void depop_all(int char_id, int eset_id);
		void aggro_all(int char_id, int eset_id, int entity_id);
		void assign_faction(int char_id, int eset_id, int faction_id);
		bool is_member(int char_id, int eset_id, int entity_id);
		float min_distance_to_point(int char_id, int eset_id, float x,
			float y, float z);
		void arrange_eset(int char_id, int eset_id, float range, 
			float x, float y, float z);

		// a list of all ESet_Records containing the entity
		ID_List esets_containing(int char_id, int entity_type);
};

typedef enum
{
	Modification_Loot = 0,
	Modification_Player_Visibility = 2,
	Modification_Group_Visibility = 3,
	Modification_Raid_Visibility = 4,
	Modification_Player_Immunity = 5,
	Modification_Group_Immunity = 6,
	Modification_Raid_Immunity = 7,
	Modification_OnlyUsableOn = 8,
	Modification_OnlyUsableAt = 9,
	Modification_NPC_Visibility = 10,
	Modification_Spawngroup_Visibility = 11,
	Modification_Vendor = 12
} Modification_Type;

struct Selective_Modification
{
	int char_id;
	int quest_id;
	EntityType entity_type;
	Modification_Type mod_type;

	int entity_id;
	int eset_id;		// some mods affect an entire ESet
	int index;			// for item/loot modifications
	int item_1_type;	// for loot modification
	int item_2_type;	// for loot modification
	int chance;
	float distance;		// for distance limited mods

	float x, y, z;		// for location-based mods
};

typedef std::multimap<int, Selective_Modification> Mod_Multimap;

class QModifications
{
	private:
		static std::unique_ptr<QModifications> _instance;

		Entity_Set *eset;
		Integer_MapSet char_to_eset;

		// entity -> Selective_Modification  (collisions ok)
		Mod_Multimap mod_records;
		ID_Set filtered;

		void limit_player_visibility(int char_id, int quest_id, int client_id,
			float distance);
		void limit_group_visibility(int char_id, int quest_id, int group_id,
			float distance);
		void limit_raid_visibility(int char_id, int quest_id, int raid_id,
			float distance);

		void insert_player_immunity(int char_id, int quest_id, int eset_id);
		void insert_group_immunity(int char_id, int quest_id, int group_id,
			 int eset_id);
		void insert_raid_immunity(int char_id, int quest_id, int raid_id,
			 int eset_id);

		void perform_loot_mods_for_client(NPC *npc, int char_id);
		bool is_associated(int char_1_id, int char_2_id);

		// pseudo-instancing helpers
		void show_entity_to_client(int char_id, int entity_id);
		void hide_entity_from_client(int char_id, int entity_id);
		void hide_spawngroup_from_client(int char_id, int sg_id);
	protected:
		QModifications();

	public:
		static QModifications& Instance();

		// client visibility deps on char's id and group/raid affiliation
		bool is_entity_visible(int entity_id, int char_id);

		bool is_entity_immune(int entity_id, int char_id);

		// no need to pre-query.  faster to try and fail
		void perform_loot_mods(NPC *, int char_id);
		bool is_usage_permitted(int char_id, int item_id, int entity_id);
		void process_new_npc(NPC *npc);

		void add_vendor_item(int vendor_id, int item_id, int price, 
			int quest_id, int char_id);
		void remove_vendor_item(int vendor_id, int item_id, int char_id);

		// exchange item1 for item2 for members of eset
		void insert_loot_mod(int char_id, int quest_id, int eset_id,
			int item_1, int item_2, int chance);

		// allow item to be used only on eset members
		void insert_item_limit(int char_id, int quest_id, int item_id, int eset_id);

		void insert_item_limit(int char_id, int quest_id, int item_id,
			float x, float y, float z, float threshold);

		// prevent entity from being seen within distance of player
		void limit_visibility(int char_id, int quest_id, float distance);

		// only allow certain clients to see this npc
		void limit_npc_visibility(int char_id, int quest_id, int npc_id);
		void limit_spawngroup_visibility(int char_id, int quest_id,
			int spawngroup_id);

		// eset members are invul to player actions (harm/heal)
		void insert_immunity(int char_id, int quest_id, int eset_id);

		void remove(Modification_Type type, int char_id, int quest_id, int entity_id);

		// eset
		void add_to_eset(int char_id, int eset_id,
			EntityType type, int entity_id);
		void remove_from_eset(int char_id, int eset_id, EntityType type, int entity_id);
		void depop_eset(int char_id, int eset_id);
		void aggro_eset(int char_id, int eset_id, int entity_id);
		void assign_eset_faction(int char_id, int eset_id, int faction_id);
		float min_distance_to_point(int char_id, int eset_id,
			float x, float y, float z);
		void arrange_eset(int char_id, int eset_id, float range, 
			float x, float y, float z);
		ID_List esets_containing(int npc_id, int char_id);
		bool eset_exists(int eset_id);
		void assign_spawngroup_eset(int char_id, int sg_id, int eset_id);

		// remove all eset records for this quest, depop all entities
		void cancel_task(int quest_id, int char_id);
		void unload_char(int char_id);
};

#endif
